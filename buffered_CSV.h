#include <sys/types.h>
#include <stdio.h>

/* The buffered_CSV_t type and associated functions provide a seek-less
 * interface to a CSV or CSV-like stream. The stream can be e.g. a file or
 * pipe. There are functions for getting the number, names and types of the
 * fields, as well as for iterating on the lines. */

/* Intended use is something like this:
 *
 * Create buffered_CSV_t object:
 * buffered_CSV_t *buf_csv = create_buffered_CSV(f, NULL, 0);
 * Get the field count
 * int n = buf_csv_field_count(buf_csv);
 * Get the field names
 * char ** fld_names = buf_csv_header_line_fields(buf_csv);
 * Get the values of the first data line, e.g. to infer the types 
 * char ** fld_types = buf_csv_first_data_line_fields(buf_csv);
 * Now iterate on the lines:
 * char ** line_flds;
 * while (true)  {
 *     line_flds = buf_csv_next_data_line_fields(buf_csv);
 *     if (NULL == line_flds) break;
 *     // process fields
 * }
 */

extern const int BUF_CSV_DUMP_SKIPPED;
extern const int BUF_CSV_NO_HEADER;

/* I keep details of struct buffered_CSV hidden, so I can change the
 * implementation without breaking anything. Access to members is by the
 * functions declared here. */

struct buffered_CSV;
typedef struct buffered_CSV buffered_CSV_t;

/* Creates a buffered_CSV_t structure and returns a pointer to it, or NULL in
 * case of problems. If 'first_line_regexp' is not NULL, leading lines are
 * skipped until a line matches. The program then proceeds as if this line had
 * been the first of a true CSV file. The 'flags' is a bit array in which any
 * of BUF_CSV_DUMP_SKIPPED and BUF_CSV_NO_HEADER can be set. If
 * BUF_CSV_DUMP_SKIPPED is set, any skipped lines will be output to stdout. If
 * BUF_CSV_NO_HEADER is set, the first line is considered data, and a header
 * line is generated, with field names of the form "f1", "f2", etc. */

buffered_CSV_t *create_buffered_CSV(FILE *, char separator,
		char *first_line_regexp, int flags);

/* Line-wise functions */

/* These functions also have field-wise equivalents, which do the tokenizing
 * job for you and return a char** with the field values (see 'Field-wise
 * functions' below)  */

/*  Returns a copy of the header line. You need to free() it. */

char *buf_csv_header_line(buffered_CSV_t *);

/*  Returns a copy of the first data line. You need to free() it. */

char *buf_csv_first_data_line(buffered_CSV_t *);

/* Returns the next data (i.e., non-header) line in *lineptr, starting with the
 * *first* one (even if buf_csv_first_data_line() is called before).
 * Iteratively calling this function will return each data line in turn, until
 * the end of the stream is reached.  Returns the number of characters read, or
 * -1 if nothing was read (which signals EOF). */

ssize_t buf_csv_next_data_line(char **lineptr, buffered_CSV_t *);

/* Field-wise functions */

int buf_csv_field_count(buffered_CSV_t *);

/*  Returns a char** containing each of the header fields, i.e. field
 *  names. If the constructor was passed BUF_CSV_NO_HEADER, these will be
 *  generated automatically, and named "f1", "f2", etc. */

char **buf_csv_header_fields(buffered_CSV_t *);

/*  Returns a char** containing the values of the fields of the first data
 *  line. */

char **buf_csv_first_data_line_fields(buffered_CSV_t *);

/* Returns a char** containing the values of each field of the next data (i.e.,
 * non-header) line, starting with the *first* one (even if
 * buf_csv_first_data_line() or buf_csv_first_data_line_fields() is
 * called before).  Iteratively calling this function will return the values of
 * each data line in turn, until the end of the stream is reached.  Returns
 * NULL if no more line was read. */

char **buf_csv_next_data_line_fields(buffered_CSV_t *);

/* Misc functions */

/* Calls feof() on the associated FILE*, and returns its value */

int buf_csv_eof(buffered_CSV_t *);

/* Closes the associated FILE* */

void buf_csv_close(buffered_CSV_t *);

/* Destroys the buffered_CSV structure */

void destroy_buffered_CSV(buffered_CSV_t *);
