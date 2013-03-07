#define _GNU_SOURCE

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <regex.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#include "buffered_CSV.h"

#define SKIP_SUCCESS 0

const int BUF_CSV_DUMP_SKIPPED = 1;
const int BUF_CSV_NO_HEADER = 2;

/* This structure adds buffering of the first two lines of a FILE* structure,
 * corresponding to the CSV header and first line.  This allows the file's data
 * to be visited in a loop, without having to treat the first (few) lines
 * differently before looping, and also without ever needing to seek(). This
 * means that unseekable streams, such as pipes, also work. I keep the
 * structure in this file so users need not (and cannot) know the details. */

struct buffered_CSV {
	FILE *csv;
	char separator;
	char *header_line;
	int num_fields;
	char *first_data_line;
	int lines_read;
	int field_count;
};

static int skip_ignored_leading_lines(FILE *csv, char *first_line_re,
		bool print_skipped, char **header_line)
{
	regex_t preg;
	int result;

	result = regcomp(&preg, first_line_re, REG_EXTENDED | REG_NOSUB);
	if (0 != result) return result;

	char *csv_line = NULL;
	size_t len = 0;
	while (-1 != getline(&csv_line, &len, csv)) {
		result = regexec(&preg, csv_line, 0, NULL, 0);
		switch(result) {
		case 0: /* match */
			*header_line = csv_line;
			regfree(&preg);
			return SKIP_SUCCESS;
		case REG_NOMATCH:
			/* proceed to next line, storing position */
			if (print_skipped) printf("%s", csv_line);
			break;
		default:
			free(csv_line);
			regfree(&preg);
			return result;
		}
		free(csv_line);
		csv_line = NULL;
		len = 0;
	}
	free(csv_line);
	regfree(&preg);

	/* if we get here without a match, one could argue that there is
	 * something wrong; however the problem does not lie within this
	 * function. */
	return result;
}

/* returns a char** with "f1", "f2", etc. Don't forget to free() */

static char **construct_numeric_field_names(int num_fields)
{
	/* find the space needed for the field names, like f1..f20, etc. */
	int max_num_len = (int) ceil(log10(num_fields));
	int max_fld_len = max_num_len + 1; /* leading 'f' */

	char ** field_names = malloc(num_fields * sizeof(char *));
	if (NULL == field_names) return NULL;

	for (int i = 0; i < num_fields; i++) {
		/* +1 for trailing '\0' */
		char *fld_name = malloc((max_fld_len+1) * sizeof(char));
		if (NULL == fld_name) return NULL;
		sprintf(fld_name, "f%d", i+1);
		field_names[i] = fld_name;
	}

	return field_names;
}

buffered_CSV_t *create_buffered_CSV(FILE * csv, char separator,
		char *first_line_re, int flags)
{
	buffered_CSV_t *buf_csv = malloc(sizeof(buffered_CSV_t));
	if (NULL == buf_csv) return NULL;

	buf_csv->csv = csv;
	buf_csv->separator = separator;
	buf_csv->field_count = 0;

	char *csv_line = NULL;
	size_t len = 0;

	if (NULL == first_line_re) {
		if (-1 == getline(&csv_line, &len, csv)) return NULL; 
	} else {
		if (SKIP_SUCCESS != skip_ignored_leading_lines(csv,
				first_line_re, flags & BUF_CSV_DUMP_SKIPPED,
				&csv_line))
			return NULL;
	}

	/* csv_line now points to the first CSV line, which is usually a
	 * header, but may already be data. At least we can count the fields: */

	for (char *p = csv_line; p; p = index(p + 1, buf_csv->separator))
		buf_csv->field_count++;

	if (flags & BUF_CSV_NO_HEADER) {
		/* csv_line is data */
		buf_csv->header_line = NULL;
		buf_csv->first_data_line = strdup(csv_line);
		free(csv_line);
	} else {
		/* csv_line is header */
		buf_csv->header_line = strdup(csv_line);
		free(csv_line);
		csv_line = NULL;
		if (-1 == getline(&csv_line, &len, csv)) return NULL; 
		buf_csv->first_data_line = strdup(csv_line);
		free(csv_line);
	}

	buf_csv->lines_read = 0;

	return buf_csv;
}

void destroy_buffered_CSV(buffered_CSV_t *buf_csv)
{
	fclose(buf_csv->csv);
	free(buf_csv->header_line);
	free(buf_csv->first_data_line);
	free(buf_csv);
}

int buf_csv_field_count(buffered_CSV_t *buf_csv)
{
	return buf_csv->field_count;
}

char *buf_csv_header_line(buffered_CSV_t *buf_csv)
{
	return strdup(buf_csv->header_line);
}

char *buf_csv_first_data_line(buffered_CSV_t *buf_csv)
{
	return strdup(buf_csv->first_data_line);
}

ssize_t buf_csv_next_data_line(char **lineptr, buffered_CSV_t *buf_csv)
{
	ssize_t read_length;
	size_t len = 0;

	if (0 == buf_csv->lines_read) {
		*lineptr = strdup(buf_csv->first_data_line);
		read_length = strlen(*lineptr);
		// fprintf(stderr, "read '%s' (length %d)\n", *lineptr, read_length);
	} else {
		read_length = getline(lineptr, &len, buf_csv->csv);
	}

	buf_csv->lines_read++;

	return read_length;
}

/* An alternative to strtok() (which modifies its input and doesn't handle
 * empty fields). This function does not modify its input, and does handle
 * empty fields (including leading and trailing empty fields). It also takes
 * advantage of the fact that the number of fields in a table is constant, and
 * already known when the function is called. */

// TODO maybe a name that refers to fields would be better, e.g. parse_fields.
// TODO make this f() quote-aware.

/* Testing:

 The following are known to work (notice some empty fields):

 tokenize("alpha,beta,gamma,delta,epsilon", ',', 5);
 tokenize("alpha,beta,,delta,epsilon", ',', 5);
 tokenize(",beta,gamma,delta,epsilon", ',', 5);
 tokenize("alpha,beta,gamma,delta,", ',', 5);
 tokenize("alpha,beta,,,epsilon", ',', 5);

 */

static char ** tokenize(char *line_orig, char sep, int num_fields)
{
	char *line = strdup(line_orig);
	if (NULL == line) return NULL;

	int i;
	char *fld_start, *fld_end;
	fld_start = fld_end = line;
	//printf("%s", line);
	char ** fields = malloc(num_fields * sizeof(char*));
	if (NULL == fields) return NULL;

	for (i = 0; i < num_fields - 1; i++) { /* last field is special */
		fld_end = index(fld_start, sep);
		if (NULL == fld_end) return NULL; /* too few fields */
		size_t flen = fld_end - fld_start;
		char *f = malloc((flen+1) * sizeof(char));
		if (NULL == f) return NULL;
		strncpy(f, fld_start, flen);
		f[flen] = '\0';
		fields[i] = f;
		//printf("fld @ %d-%d (%d): %s\n", fld_start-line, fld_end-line, flen, f);
		
		fld_start = fld_end + 1;
	}
	/* No more separator - just copy till closing '\0' */
	fields[i] = strdup(fld_start);
	/* Remove any trailing '\n' */
	char *c = rindex(fields[i], '\n');
	if (NULL != c) *c = '\0';

	free(line);

	return fields;
}

char **buf_csv_header_fields(buffered_CSV_t *buf_csv)
{
	if (NULL == buf_csv->header_line)
		/* CSV has no headers */
		return construct_numeric_field_names(buf_csv->field_count);

	char *hdr = buf_csv_header_line(buf_csv);
	if (NULL == hdr) return NULL;
	char **fields = tokenize(hdr, buf_csv->separator,
			buf_csv_field_count(buf_csv));
	free(hdr);
	return fields;
}

char **buf_csv_first_data_line_fields(buffered_CSV_t *buf_csv)
{
	char *fd = buf_csv_first_data_line(buf_csv);
	if (NULL == fd) return NULL;
	char ** fields = tokenize(fd, buf_csv->separator,
			buf_csv_field_count(buf_csv));
	free(fd);
	return fields;
}

char **buf_csv_next_data_line_fields(buffered_CSV_t *buf_csv)
{
	char *csv_line = NULL;
	ssize_t chars_read = buf_csv_next_data_line(&csv_line, buf_csv);
	if (-1 == chars_read) { free(csv_line) ; return NULL; }

	char **result = tokenize(csv_line, buf_csv->separator,
		buf_csv_field_count(buf_csv));
	free(csv_line);

	return(result);
}


int buf_csv_eof(buffered_CSV_t *buf_csv) { return feof(buf_csv->csv); }


#ifdef TEST_BUFFERED_CSV

char *exp_hdr = "Genus\tnb_species\n";
char *exp_1st_data = "Cercopithecus\t26\n";
char *exp_2nd_data = "Simias\t1\n";
char *exp_3rd_data = "Pan\t2\n";
char *exp_4th_data = "Pongo\t2\n";
char *exp_5th_data = "Colobus\t5\n";

/* This is the old interface, in which the client had to split the lines. The
 * new interface returns arrays of strings (i.e., char **), one string per
 * field in the CSV line. */

int test_plain_CSV_by_lines()
{
	const char *test_name = __func__;

	FILE * csv = fopen("data/test_buffered_CSV.csv", "r");
	if (NULL == csv) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}

	buffered_CSV_t *buf_csv = create_buffered_CSV(csv, '\t', NULL, 0);
	if (NULL == buf_csv) {
		printf ("%s: buf_csv should not be NULL.\n", test_name);
		return 1;
	}
	if (0 != strcmp(buf_csv_header_line(buf_csv), exp_hdr)) {
		printf ("%s: header should be '%s', but is '%s'.\n", 
				test_name, exp_hdr, buf_csv->header_line);
		return 1;
	}
	if (0 != strcmp(buf_csv->first_data_line, exp_1st_data)) {
		printf ("%s: 1st data line should be '%s', but is '%s'.\n", 
				test_name, exp_1st_data, buf_csv->first_data_line);
		return 1;
	}
	if (0 != buf_csv->lines_read) {
		printf("%s: %d lines read, should be 0\n", test_name,
				buf_csv->lines_read);
		return 1;
	}

	ssize_t chars_read;

	char *csv_line = NULL;
	chars_read = buf_csv_next_data_line(&csv_line, buf_csv);
	if ((ssize_t) strlen(csv_line) != chars_read) {
		printf("%s: (line 1) expected %d chars, got %d\n", test_name,
				(int) strlen(csv_line), (int) chars_read);
		return 1;
	}
	if (0 != strcmp(exp_1st_data, csv_line)) {
		printf ("%s: (line 1) expected '%s', got '%s'\n", test_name,
				exp_1st_data, csv_line);
		return 1;
	}

	csv_line = NULL;
	chars_read = buf_csv_next_data_line(&csv_line, buf_csv);
	if ((ssize_t) strlen(csv_line) != chars_read) {
		printf("%s: (line 2) expected %d chars, got %d\n", test_name,
				(int) strlen(csv_line), (int) chars_read);
		return 1;
	}
	if (0 != strcmp(exp_2nd_data, csv_line)) {
		printf ("%s: (line 2) expected '%s', got '%s'\n", test_name,
				exp_2nd_data, csv_line);
		return 1;
	}

	csv_line = NULL;
	chars_read = buf_csv_next_data_line(&csv_line, buf_csv);
	if ((ssize_t) strlen(csv_line) != chars_read) {
		printf("%s: (line 3) expected %d chars, got %d\n", test_name,
				(int) strlen(csv_line), (int) chars_read);
		return 1;
	}
	if (0 != strcmp(exp_3rd_data, csv_line)) {
		printf ("%s: (line 3) expected '%s', got '%s'\n", test_name,
				exp_3rd_data, csv_line);
		return 1;
	}

	csv_line = NULL;
	chars_read = buf_csv_next_data_line(&csv_line, buf_csv);
	if ((ssize_t) strlen(csv_line) != chars_read) {
		printf("%s: (line 4) expected %d chars, got %d\n", test_name,
				(int) strlen(csv_line), (int) chars_read);
		return 1;
	}
	if (0 != strcmp(exp_4th_data, csv_line)) {
		printf ("%s: (line 4) expected '%s', got '%s'\n", test_name,
				exp_4th_data, csv_line);
		return 1;
	}

	csv_line = NULL;
	chars_read = buf_csv_next_data_line(&csv_line, buf_csv);
	if ((ssize_t) strlen(csv_line) != chars_read) {
		printf("%s: (line 5) expected %d chars, got %d\n", test_name,
				(int) strlen(csv_line), (int) chars_read);
		return 1;
	}
	if (0 != strcmp(exp_5th_data, csv_line)) {
		printf ("%s: (line 5) expected '%s', got '%s'\n", test_name,
				exp_5th_data, csv_line);
		return 1;
	}

	csv_line = NULL;
	chars_read = buf_csv_next_data_line(&csv_line, buf_csv);
	if (-1 != chars_read) {
		printf("%s: (EOF) expected -1 chars, got %d\n", test_name,
				(int) chars_read);
		return 1;
	}

	printf ("%s: ok.\n", test_name);
	return 0;
}

int test_plain_CSV()
{
	const char *test_name = __func__;

	FILE * csv = fopen("data/test_buffered_CSV.csv", "r");
	if (NULL == csv) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}
	char ** flds;

	buffered_CSV_t *buf_csv = create_buffered_CSV(csv, '\t', NULL, 0);
	if (NULL == buf_csv) {
		printf ("%s: buf_csv should not be NULL.\n", test_name);
		return 1;
	}

	if (2 != buf_csv_field_count(buf_csv)) {
		printf ("%s: expected 2 fields, got %d.\n", test_name,
				buf_csv_field_count(buf_csv));
		return 1;
	}

	flds = buf_csv_header_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Genus", flds[0])) {
		printf ("%s: expected 'Genus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("nb_species", flds[1])) {
		printf ("%s: expected 'nb_Species', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_first_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Cercopithecus", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("26", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Cercopithecus", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("26", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Simias", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("1", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Pan", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("2", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Pongo", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("2", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Colobus", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("5", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}


	printf ("%s: ok.\n", test_name);
	return 0;
}

int test_no_headers_CSV()
{
	const char *test_name = __func__;

	FILE * csv = fopen("data/test_buffered_CSV_nohdr.csv", "r");
	if (NULL == csv) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}
	char ** flds;

	buffered_CSV_t *buf_csv = create_buffered_CSV(csv, '\t', NULL,
			BUF_CSV_NO_HEADER);
	if (NULL == buf_csv) {
		printf ("%s: buf_csv should not be NULL.\n", test_name);
		return 1;
	}

	if (2 != buf_csv_field_count(buf_csv)) {
		printf ("%s: expected 2 fields, got %d.\n", test_name,
				buf_csv_field_count(buf_csv));
		return 1;
	}

	flds = buf_csv_header_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("f1", flds[0])) {
		printf ("%s: expected 'f1', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("f2", flds[1])) {
		printf ("%s: expected 'f2', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_first_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Cercopithecus", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("26", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Cercopithecus", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("26", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Simias", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("1", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Pan", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("2", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Pongo", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("2", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Colobus", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("5", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}


	printf ("%s: ok.\n", test_name);
	return 0;
}

int test_skip_leading()
{
	const char *test_name = __func__;

	FILE * csv = fopen("data/test_buffered_CSV_re.csv", "r");
	if (NULL == csv) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}
	char ** flds;

	buffered_CSV_t *buf_csv = create_buffered_CSV(csv, '\t', "^[A-Z]", 0);
	if (NULL == buf_csv) {
		printf ("%s: buf_csv should not be NULL.\n", test_name);
		return 1;
	}

	if (2 != buf_csv_field_count(buf_csv)) {
		printf ("%s: expected 2 fields, got %d.\n", test_name,
				buf_csv_field_count(buf_csv));
		return 1;
	}

	flds = buf_csv_header_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Genus", flds[0])) {
		printf ("%s: expected 'Genus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("nb_species", flds[1])) {
		printf ("%s: expected 'nb_Species', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_first_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Cercopithecus", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("26", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Cercopithecus", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("26", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Simias", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("1", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Pan", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("2", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Pongo", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("2", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Colobus", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("5", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}


	printf ("%s: ok.\n", test_name);
	return 0;
}

int test_no_headers_skip_leading() 
{
	const char *test_name = __func__;

	FILE * csv = fopen("data/test_buffered_CSV_re_nohdr.csv", "r");
	if (NULL == csv) {
		perror(NULL);
		exit(EXIT_FAILURE);
	}
	char ** flds;

	buffered_CSV_t *buf_csv = create_buffered_CSV(csv, '\t', "^[A-Z].*\t",
			BUF_CSV_NO_HEADER);
	if (NULL == buf_csv) {
		printf ("%s: buf_csv should not be NULL.\n", test_name);
		return 1;
	}

	if (2 != buf_csv_field_count(buf_csv)) {
		printf ("%s: expected 2 fields, got %d.\n", test_name,
				buf_csv_field_count(buf_csv));
		return 1;
	}

	flds = buf_csv_header_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("f1", flds[0])) {
		printf ("%s: expected 'f1', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("f2", flds[1])) {
		printf ("%s: expected 'f2', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_first_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Cercopithecus", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("26", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Cercopithecus", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("26", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Simias", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("1", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Pan", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("2", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Pongo", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("2", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}

	flds = buf_csv_next_data_line_fields(buf_csv);
	assert (NULL != flds);	/* might fail silently otherwise */
	if (0 != strcmp("Colobus", flds[0])) {
		printf ("%s: expected 'Cercopithecus', got '%s'.\n", test_name, 
				flds[0]);
		return 1;
	}
	if (0 != strcmp("5", flds[1])) {
		printf ("%s: expected '26', got '%s'.\n", test_name, 
				flds[1]);
		return 1;
	}


	printf ("%s: ok.\n", test_name);
	return 0;
}

int main ()
{
	int failures = 0;

	failures += test_plain_CSV_by_lines();
	failures += test_plain_CSV();
	failures += test_no_headers_CSV();
	failures += test_skip_leading();
	failures += test_no_headers_skip_leading();

	if (0 == failures)
		printf("buffered_CSV tests ok.\n");
	else
		printf("buffered_CSV: %d test(s) FAILED.\n", failures);
}

#endif



