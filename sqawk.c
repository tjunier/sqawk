/* only for CentoOS 5 (sigh...) getline() is now (since 2008) POSIX */
#define _GNU_SOURCE

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/types.h>
#include <regex.h>
#include <limits.h>

#include "sqlite3.h"
#include "buffered_CSV.h"

#define MEM_DATABASE ":memory:"
#define DISK_DATABASE "sqawk.db"

#define MAX_FILES 16	/* Should be ok for a while... */

#define NUM_TYPE "NUMERIC"
#define TEXT_TYPE "TEXT"

#define WHOLE_FILE -1

/* run switches */
static const int sw_verbose = 1 << 1;
static const int sw_dry_run = 1 << 2;
static const int sw_show_sql = 1 << 3;
static const int sw_enable_foreign_keys = 1 << 4;

/* file switches */
static const int fsw_no_headers = 1 << 1;
static const int fsw_literal_col_names = 1 << 2;
static const int fsw_show_skipped_lines = 1 << 3;

struct file_params {
	char *filename;
	char separator;
	char *index_fields;
	int file_switches;
	char *first_line_re;
	char *text_fields;
	char *primary_key_fields;
	char *foreign_key;
	char *fk_referent;
	char *alias;
};


struct parameters {
	/* Run parameters, that is. */
	int switches;	/* bit field for switches */
	char *database;
	struct file_params *files;
	int num_files;
	int chunk_size;	/* flush last table every n rows */
	char *user_sql;
};

static void die(const char *msg)
{
	if (NULL == msg)
		perror(NULL);
	else
		fprintf(stderr, "FATAL: %s\n", msg);

	exit(EXIT_FAILURE);
}

/*
static void warn(const char *msg)
{
	if (NULL == msg)
		perror (NULL);
	else
		fprintf(stderr, "WARNING: %s\n", msg);
}
*/

/* debug f() that shows n chars from a file */

/*
static char * show_file(FILE* f, int n)
{
	char buf[n+1];
	bzero(buf, n+1);
	long fpos = ftell(f);
	fread(buf, sizeof(char), n, f);
	fseek(f, fpos, SEEK_SET);
	return(strdup(buf));
}
*/

static struct parameters *parse_arguments(int argc, char **argv)
{
	struct parameters *params = malloc(sizeof(struct parameters));
	if (NULL == params) die (NULL);

	params->switches = 0;
	params->database = MEM_DATABASE;	/* don't use disk */
	params->files = calloc(MAX_FILES, sizeof(struct file_params));
	if (NULL == params->files) die (NULL);
	params->chunk_size = WHOLE_FILE;

	int file_num = 0;
	// TODO: refactor this (done also at end of loop). In fact, is this
	// even necessary, given that we use calloc() to allocate
	// params->files? YES, because we use direct structures instead of
	// pointers, as we should have done (i.e., params->files should be
	// struct file_params**.
	params->files[file_num].index_fields = NULL;
	params->files[file_num].separator = '\t';
	params->files[file_num].file_switches = 0;
	params->files[file_num].first_line_re = NULL;
	params->files[file_num].text_fields = NULL;
	params->files[file_num].primary_key_fields = NULL;
	params->files[file_num].foreign_key = NULL;
	params->files[file_num].fk_referent = NULL;
	params->files[file_num].alias = NULL;

	int argn;
	/* 1: skips prog name; -1: last arg is SQL */
	for (argn = 1; argn < argc - 1; argn++) { 
		/* the strlen() call is for treating "-" as a placeholder for
		 * stdin, while -[a-zA-Z] are options. */
		if ('-' == argv[argn][0] && strlen(argv[argn]) > 1) {
			if (0 == strcmp("-v", argv[argn]))
				params->switches |= sw_verbose;
			else if (0 == strcmp("-n", argv[argn]))
				params->switches |= sw_dry_run;
			else if (0 == strcmp("-k", argv[argn]))
				params->database = DISK_DATABASE;
			else if (0 == strcmp("-q", argv[argn]))
				params->switches |= sw_show_sql;
			else if (0 == strcmp("-P", argv[argn])) {
				argn++;
				params->chunk_size = atoi(argv[argn]);
			}
			else if (0 == strcmp("-a", argv[argn])) {
				argn++;
				params->files[file_num].alias = strdup(argv[argn]);
			}
			else if (0 == strcmp("-i", argv[argn])) {
				argn++;
				params->files[file_num].index_fields = strdup(argv[argn]);
			}
			else if (0 == strcmp("-f", argv[argn])) {
				argn++;
				params->files[file_num].first_line_re = strdup(argv[argn]);
			}
			else if (0 == strcmp("-F", argv[argn])) {
				argn++;
				params->files[file_num].first_line_re = strdup(argv[argn]);
				params->files[file_num].file_switches
					|= fsw_show_skipped_lines;
			}
			else if (0 == strcmp("-l", argv[argn])) {
				params->files[file_num].file_switches 
					|= fsw_literal_col_names;
			}
			else if (0 == strcmp("-H", argv[argn])) {
				params->files[file_num].file_switches 
					|= fsw_no_headers;
			}
			else if (0 == strcmp("-s", argv[argn])) {
				argn++;
				params->files[file_num].separator =
					argv[argn][0];
			}
			else if (0 == strcmp("-t", argv[argn])) {
				argn++;
				params->files[file_num].text_fields =
					strdup(argv[argn]);
			}
			else if (0 == strcmp("-p", argv[argn])) {
				argn++;
				params->files[file_num].primary_key_fields =
					strdup(argv[argn]);
			}
			else if (0 == strcmp("-K", argv[argn])) {
				argn++;
				params->files[file_num].foreign_key =
					strdup(argv[argn]);
				argn++;
				params->files[file_num].fk_referent =
					strdup(argv[argn]);
				params->switches |= sw_enable_foreign_keys;
			}
			else fprintf(stderr, "WARNING: unknown option %s\n",
					argv[argn]);
		} 
		else {
			/* This must be a filename */
			params->files[file_num].filename = strdup(argv[argn]);
			file_num++;
			/* Reset for next file, if any */
			params->files[file_num].index_fields = NULL;
			params->files[file_num].separator = '\t';
			params->files[file_num].file_switches = 0;
			params->files[file_num].first_line_re = NULL;
			params->files[file_num].text_fields = NULL;
			params->files[file_num].primary_key_fields = NULL;
			params->files[file_num].foreign_key = NULL;
		}
	}
	params->num_files = file_num;

	params->user_sql = strdup(argv[argn]);

	return params;
}

static void show_params(struct parameters *params)
{
	printf("dry run:\t%c\n", params->switches & sw_dry_run ? 'T' : 'F');
	printf("verbose:\t%c\n", params->switches & sw_verbose ? 'T' : 'F');
	printf("show generated SQL:\t%c\n", params->switches & sw_show_sql ? 'T' : 'F');
	printf("database:\t%s\n", params->database);
	if (WHOLE_FILE != params->chunk_size) 
		printf("last table flushed every %d rows.\n", params->chunk_size);
	printf("\n");
	printf("%d file(s):\n", params->num_files);
	for (int i = 0; i < params->num_files; i++) {
		struct file_params fp = params->files[i];
		if (0 == strcmp("-", fp.filename))
			printf("\tstdin");
		else
			printf("\t%s", fp.filename);
		if (NULL == fp.index_fields)
			printf (", not indexed");
		else
			printf (", indexed on %s", fp.index_fields);
		printf(", separated by ");
		if ('\t' == fp.separator)
			printf("TAB");
		else 
			printf("'%c'", fp.separator);
		if (NULL == fp.first_line_re)
			printf(", no lines skipped");
		else {
			printf(", skip to %s", fp.first_line_re);
			if (fp.file_switches & fsw_show_skipped_lines) 
				printf ("(skipped lines shown)");
		}
		if (NULL != fp.text_fields)
			printf(", field(s) '%s' forced to TEXT",
				fp.text_fields);
		if (fp.file_switches & fsw_literal_col_names)
			printf (", literal column names");
		if (NULL != fp.primary_key_fields)
			printf (", PRIMARY KEY %s", fp.primary_key_fields);
		if (NULL != fp.foreign_key) 
			printf (", foreign key '%s' on '%s'",
				fp.foreign_key, fp.fk_referent);
		if (NULL != fp.alias)
			printf (", aliased to '%s'", fp.alias);
		
		printf(".\n");
	}
	printf("\n");
	printf("user SQL:\t%s\n", params->user_sql);
}


/* An info f() that shows the contents of a char** */

static void show_char_array(char **array, int length, char *name)
{
	if (NULL == array) {
		printf("%s is empty.\n", name);
		return;
	}

	for (int i = 0; i < length; i++) {
		if (NULL != array[i])
			printf ("%s[%d]: %s\n", name, i, array[i]);
		else
			printf ("%s[%d]: NULL\n", name, i);
		fflush(stdout);
	}
}

/* make "free" name (file name, field name, etc) into a valid SQL table or
 * column name. TODO: actually, SQL seems to be pretty tolerant regarding
 * names, at least compared to C, etc. This function might not be needed. */

static char * free2SQL(char *freename)
{
	char *valid = calloc(strlen(freename) + 1, sizeof(char));
	if (NULL == valid) return NULL;

	char *p, *f;
	for (p = freename, f = valid; '\0' != *p; p++) 
		switch (*p) {
		case '-':
		case ' ':
		case '.':
			*f = '_';
			f++;
			break;
		case '#':
			break;
		default:
			*f = *p;
			f++;
			break;
		}

	return valid;
}

/*
static char * quote(char *freename)
{
	char *quoted = calloc(strlen(freename) + 3, sizeof(char));
	if (0 > sprintf(quoted, "'%s'", freename)) return NULL;

	return quoted;
}
*/

static void free_string_array(char ** array, int num_strings)
{
	for (int i = 0;  i < num_strings; i++)
		free(array[i]);
	free(array);
}

static bool is_string_numeric(const char *s)
{
    if (NULL == s || '\0' == *s || isspace(*s))
      return false;
    char * p;
    strtod (s, &p);
    return '\0' == *p;
}

static char ** get_column_types(char **field_values, int num_fields)
{
	char ** field_types = malloc(num_fields * sizeof(char *));
	if (NULL == field_types)
		return NULL;
	for (int i = 0; i < num_fields; i++) {
		if (is_string_numeric(field_values[i]))
			field_types[i] = strdup(NUM_TYPE);
		else
			field_types[i] = strdup(TEXT_TYPE);
	}


	free_string_array(field_values, num_fields);

	return field_types;
}

/* A series of heuristic rules to get a table name from a filename */

static char *filename2tablename(const char *filename)
{
	char *filename_dup = NULL;
	if (0 == strcmp("-", filename)) 
		filename_dup = strdup("stdin");
	else
		filename_dup = strdup(filename);
	if (NULL == filename_dup) return NULL;

	char *start;
	/* find the last '/' (=~ basename) */
	char *last_slash = rindex(filename_dup, '/');
	if (NULL == last_slash)
		start = filename_dup; /* filename has no dir part */
	else
		start = last_slash + 1;	/* start just after the last '/' */

	/* get rid of extension, if any (anything after last '.') */ 
	char * rd = rindex (start, '.');
	if (NULL != rd) *rd = '\0';

	char *tbl_name = strdup(start);
	free(filename_dup);

	char *valid_tbl_name = free2SQL(tbl_name);
	free(tbl_name);

	return valid_tbl_name;
}

static char *concat_string_array(char**array, int num_strings)
{
	/* Determine total length */

	int str_len = 0;
	for (int i = 0; i < num_strings; i++)
		str_len += strlen(array[i]);

	char *s = calloc(str_len+1, sizeof(char));
	if (NULL == s) return NULL;

	for (int i = 0; i < num_strings; i++)
		strcat(s, array[i]);

	return s;
}

static char *mk_tbl_name_part(char *tbl_name)
{
	int len = strlen("CREATE TABLE  (") + strlen(tbl_name) + 1;
	char *tbl_name_part = malloc(len * sizeof(char));
	if (NULL == tbl_name_part) return NULL;
	sprintf(tbl_name_part, "CREATE TABLE %s (", tbl_name);
	return tbl_name_part;
}

static char *mk_field_def_part(int num_fields, char ** field_names,
		char **field_types) 
{
	/* num_field names + num_field " " + num_field types + (num_field - 1)
	 * commas */
	int num_strings = 4 * num_fields - 1;
	char ** strings = calloc(num_strings, sizeof(char*));
	if (NULL == strings) return NULL;

	strings[0] = strdup(field_names[0]);
	strings[1] = strdup(" ");
	strings[2] = strdup(field_types[0]);

	// TODO: check values of strdup()s
	int fld_def, fld;
	for (fld_def = 3, fld = 1; fld_def < num_strings; fld_def++) {
		strings[fld_def] = strdup(", ");
		fld_def++;
		strings[fld_def] = strdup(field_names[fld]);
		fld_def++;
		strings[fld_def] = strdup(" ");
		fld_def++;
		strings[fld_def] = strdup(field_types[fld]);
		fld++;
	}

	//show_char_array(strings, num_strings, "field def strings");
	char *result = concat_string_array(strings, num_strings);
	free_string_array(strings, num_strings);
	return result;
}

static char *mk_constraints_part(char *primary_key_fields, char *foreign_key,
		char *fk_referent)
{
	char *primary_key_constraint, *foreign_key_constraint;
	int prim_k_cstr_len, foreign_k_cstr_len;

	if (NULL == primary_key_fields) {
		prim_k_cstr_len = 0;
		primary_key_constraint = strdup("");
		if (NULL == primary_key_constraint) return NULL;
	} else {
		prim_k_cstr_len = strlen(", PRIMARY KEY ()") +
			strlen(primary_key_fields) + 1;
		primary_key_constraint = malloc(prim_k_cstr_len * sizeof(char));
		if (NULL == primary_key_constraint) return NULL;
		sprintf(primary_key_constraint, ", PRIMARY KEY (%s)", primary_key_fields);
	}

	// TODO: finish adapting this from above
	if (NULL == foreign_key) {
		foreign_k_cstr_len = 0;
		foreign_key_constraint = strdup("");
		if (NULL == primary_key_constraint) return NULL;
	} else {
		// referent can't be NULL if foreign key != NULL
		assert (NULL != fk_referent); 
		foreign_k_cstr_len = strlen(", FOREIGN KEY () REFERENCES ") +
			strlen(foreign_key) + strlen (fk_referent) + 1;
		foreign_key_constraint =
			malloc(foreign_k_cstr_len * sizeof(char));
		if (NULL == foreign_key_constraint) return NULL;
		sprintf(foreign_key_constraint,
			", FOREIGN KEY (%s) REFERENCES %s",
			foreign_key, fk_referent);
	}

	char *constraints_part = malloc(
		(prim_k_cstr_len + foreign_k_cstr_len + 1) * sizeof(char));
	if (NULL == constraints_part) return NULL;

	*constraints_part = '\0';
	strcat(constraints_part, primary_key_constraint);
	strcat(constraints_part, foreign_key_constraint);

	free(primary_key_constraint);
	free(foreign_key_constraint);

	return constraints_part;
}

static char *construct_create_tbl_SQL(const char *tbl_name, int num_fields,
		char ** field_names, char ** field_types,
		char *primary_key_fields, char *foreign_key, char *fk_referent)
{
	char *table_name = strdup(tbl_name);
	if (NULL == table_name) { perror(NULL); exit(EXIT_FAILURE); }

	char **sql_parts = malloc(4 * sizeof(char*));
	if (NULL == sql_parts) { perror(NULL); exit(EXIT_FAILURE); }

	char *part;
	part = mk_tbl_name_part(table_name);
	free(table_name);
	if (NULL == part) { perror(NULL); exit(EXIT_FAILURE); }
	sql_parts[0] = part;
	part = mk_field_def_part(num_fields, field_names, field_types);
	if (NULL == part) { perror(NULL); exit(EXIT_FAILURE); }
	sql_parts[1] = part;
	part = mk_constraints_part(primary_key_fields, foreign_key,
			fk_referent);
	if (NULL == part) { perror(NULL); exit(EXIT_FAILURE); }
	sql_parts[2] = part;
	part = strdup(");");
	if (NULL == part) { perror(NULL); exit(EXIT_FAILURE); }
	sql_parts[3] = part;
	
	//show_char_array(sql_parts, 4, "CREATE SQL");
	char * query_buf = concat_string_array(sql_parts, 4);
	//printf("CREATE SQL: %s\n", query_buf);
	free_string_array(sql_parts, 4);

	return query_buf;
}

static char *construct_insert_SQL(const char *tbl_name, int num_fields)
		
{
	/* Determine the number of strings to concatenate. This is the prefix,
	 * the table name, the  fields (plus their leading '@'s), etc. */

	int num_strings = 3 + num_fields;
	char ** strings = malloc(num_strings * sizeof(char*));
	if (NULL == strings) return NULL;
	
	strings[0] = strdup("INSERT INTO ");
	strings[1] = strdup(tbl_name);
	strings[2] = strdup(" VALUES (?");

	int i;
	for (i = 3; i < num_strings-1; i++)
		strings[i] = strdup(", ?");

	strings[i] = strdup(")");

	char *query_buf = concat_string_array(strings, num_strings);
	free_string_array(strings, num_strings);

	if (NULL == query_buf) return NULL;
	
	return query_buf;
}

static char *construct_index_SQL(const char *tbl_name, char *ndx_flds)
{
	int num_strings = 9;
	char ** query_elems = calloc(num_strings, sizeof(char *));
	if (NULL == query_elems) return NULL;

	query_elems[0] = strdup("CREATE INDEX '");
	query_elems[1] = strdup(tbl_name);
	query_elems[2] = strdup(":");
	query_elems[3] = strdup(ndx_flds);
	query_elems[4] = strdup("' ON ");
	query_elems[5] = strdup(tbl_name);
	query_elems[6] = strdup(" (");
	query_elems[7] = strdup(ndx_flds);
	query_elems[8] = strdup(")");

	char *query = concat_string_array(query_elems, num_strings);

	free_string_array(query_elems, num_strings);

	return query;
}

/*
static void skip_line (FILE *csv)
{
	char *csv_line = NULL;
	size_t len = 0;
	getline(&csv_line, &len, csv);
	free(csv_line);
}
*/

static void create_file_table(sqlite3 *db, const char *tbl_name, int num_fields,
	char ** col_names, char ** col_types, char *primary_key_fields,
	char *foreign_key, char *fk_referent, int run_switches)
{
	char *error_msg = NULL;

	if (run_switches & sw_verbose)
		show_char_array(col_types, num_fields, "col_types");
	if (NULL == col_types) { perror(NULL); exit (EXIT_FAILURE); }

	char * create_tbl_SQL = construct_create_tbl_SQL(tbl_name,
			num_fields, col_names, col_types, primary_key_fields,
			foreign_key, fk_referent);
	if (NULL == create_tbl_SQL) { perror(NULL); exit (EXIT_FAILURE); }

	if (run_switches & sw_show_sql)
		printf("-- Create table:\n%s\n", create_tbl_SQL);
	if (run_switches & sw_dry_run) {free(create_tbl_SQL); return;}

	int sql_result = sqlite3_exec(db, create_tbl_SQL,
			NULL, NULL, &error_msg);
	if (SQLITE_OK != sql_result) die(error_msg);

	free(create_tbl_SQL);
}

/* returns a prepared  insert statement, or NULL in case of dry run. Aborts in
 * case of problem, as the error is not recoverable. */

static sqlite3_stmt* prepare_insert_statement(sqlite3 *db, const char *tbl_name,
		int num_fields, int run_switches)
{
	sqlite3_stmt *stmt;
	const char *tail = 0;
	char * insert_SQL = construct_insert_SQL(tbl_name, num_fields);
			
	if (run_switches & sw_show_sql) printf(
		"-- Insert data ('?': SQLite C API placeholders):\n%s\n", insert_SQL);
	if (run_switches & sw_dry_run) {free(insert_SQL); return NULL;}

	int sql_result = sqlite3_prepare_v2(db, insert_SQL,
		-1, &stmt, &tail);
	if (SQLITE_OK != sql_result) die (sqlite3_errmsg(db));
	free(insert_SQL);

	return stmt;
}

// TODO: could dispense with *db by returning the error msg or code
static void insert_chunk(sqlite3 *db, buffered_CSV_t *buf_csv, int num_fields,
		int chunk_size, sqlite3_stmt *stmt)
{
	if (WHOLE_FILE == chunk_size) chunk_size = INT_MAX;

	for (int nrow = 0; nrow < chunk_size ; nrow++) {
		char ** fld_vals = buf_csv_next_data_line_fields(buf_csv);
		if (NULL == fld_vals) break;

		/* Bind all fields in turn */
		int sql_result;
		for (int i = 0; i < num_fields; i++) {
			sql_result = sqlite3_bind_text(stmt, i+1, fld_vals[i],
					-1, SQLITE_TRANSIENT);
			if (SQLITE_OK != sql_result)
				die (sqlite3_errmsg(db));
		}

		sqlite3_step(stmt);
		sqlite3_clear_bindings(stmt);
		sqlite3_reset(stmt);

		free_string_array(fld_vals, num_fields);

	}
}

// static void insert_csv_into_table(sqlite3 *db, FILE* csv, const char *tbl_name,
// 	int num_fields, char separator, int run_switches)
// {
// 
// 	sqlite3_stmt *stmt = prepare_insert_statement(db, tbl_name,
// 			num_fields, run_switches);	
// 	//if (NULL == stmt) die (sqlite3_errmsg(db));
// 	if (run_switches & sw_dry_run) return;
// 
// 	int sql_result;
// 	char *csv_line = NULL;
// 	size_t len = 0;
// 	while (-1 != getline(&csv_line, &len, csv)) {
// 		
// 		char ** fld_vals = tokenize(csv_line, separator, num_fields);
// 		free(csv_line);
// 		/* loop ends at eof or of no more values can be read */
// 		if (NULL == fld_vals) break;
// 
// 		/* Bind all fields in turn */
// 		for (int i = 0; i < num_fields; i++) {
// 			sql_result = sqlite3_bind_text(stmt, i+1, fld_vals[i],
// 					-1, SQLITE_TRANSIENT);
// 			if (SQLITE_OK != sql_result)
// 				die (sqlite3_errmsg(db));
// 		}
// 
// 		sqlite3_step(stmt);
// 		sqlite3_clear_bindings(stmt);
// 		sqlite3_reset(stmt);
// 
// 		free_string_array(fld_vals, num_fields);
// 
// 		/* Reset these for next iteration (or SIGSEGV!) */
// 		csv_line = NULL;
// 		len = 0;
// 	}
// 	free(csv_line);
// 
// 	sqlite3_finalize(stmt);
// }

static void print_headers(sqlite3_stmt *stmt)
{
	int num_col = sqlite3_column_count(stmt);
	printf("%s", sqlite3_column_name(stmt, 0));
	for (int i = 1; i < num_col; i++) {
		printf("\t%s", sqlite3_column_name(stmt, i));	
	}
	printf("\n");
}

static void print_values(sqlite3_stmt *stmt)
{
	int num_col = sqlite3_column_count(stmt);
	printf("%s", sqlite3_column_text(stmt, 0));
	for (int i = 1; i < num_col; i++) {
		printf("\t%s", sqlite3_column_text(stmt, i));	
	}
	printf("\n");
}

void execute_user_query(sqlite3 *db, char *user_sql)
{
	sqlite3_stmt *stmt = NULL;
	const char *tail;

	sqlite3_prepare_v2(db, user_sql, -1, &stmt, &tail);
	int result;
	bool first = true;
	while ((result = sqlite3_step(stmt)) != SQLITE_DONE) {
		switch (result) {
			case SQLITE_ROW:
				if (first) { /* headers */
					print_headers(stmt);
					first = false;
				}
				print_values(stmt);
				break;
			default:
				die(sqlite3_errmsg(db));
		}
	}

	sqlite3_finalize(stmt);
}

static void start_transaction(sqlite3 *db)
{
	char *error_msg = NULL;
	int sql_result = sqlite3_exec(
		db, "BEGIN TRANSACTION", NULL, NULL, &error_msg);
	if (SQLITE_OK != sql_result) die (error_msg);
}

static void stop_transaction(sqlite3 *db)
{
	char *error_msg = NULL;
	int sql_result = sqlite3_exec(
		db, "END TRANSACTION", NULL, NULL, &error_msg);
	if (SQLITE_OK != sql_result) die (error_msg);
}

static void create_index(sqlite3 *db, char *tbl_name, char *index_fields,
		int run_switches)
{
	char *create_index_SQL = construct_index_SQL(tbl_name, index_fields);
	if (run_switches & sw_show_sql)
		printf ("-- Create index:\n%s\n", create_index_SQL);
	if (run_switches & sw_dry_run) return;

	char *error_msg = NULL;
	int sql_result = sqlite3_exec(db, create_index_SQL,
			NULL, NULL, &error_msg);
	if (SQLITE_OK != sql_result) die(error_msg);

	free(create_index_SQL);
}


static char **get_column_names(buffered_CSV_t *buf_csv, bool literal)
{
	char **col_names = buf_csv_header_fields(buf_csv);
	int field_count = buf_csv_field_count(buf_csv);

	if (literal) {
		for (int i = 0; i < field_count; i++) {
			char *lit_name;
			if (-1 == asprintf(&lit_name, "'%s'", col_names[i]))
				return NULL;
			free(col_names[i]);
			col_names[i] = lit_name;
		}
	} else {
		for (int i = 0; i < field_count; i++) {
			char *sql_name = free2SQL(col_names[i]);
			free(col_names[i]);
			col_names[i] = sql_name;
		}
	}

	return col_names;
}

/* Returns the index of 'query' in 'array' */

static int index_of (char *query, char **str_array, int num_fields)
{
	int i;
	for (i = 0; i < num_fields; i++) 
		if (0 == strcmp(query, str_array[i]))
			return i;
	return -1;
}

static void coerce_to_text(char* flds, char ** col_types, char **field_names,
		int num_fields)
{
	regex_t preg;
	regmatch_t pmatch[1];

	int result;

	result = regcomp(&preg, "[^,]+", REG_EXTENDED);
	assert(0 == result);

	char *fields = strdup(flds);
	int len = strlen(fields);
	char *c;
	/* iteratively try to match the regexp on the string */
	for (c = fields; (c-fields) < len-1; ) {
		result = regexec(&preg, c, 1, pmatch, 0);
		switch (result) {
		case 0: /* match */
			c[pmatch[0].rm_eo] = '\0';
			int n = -1;
			if (is_string_numeric(c))
				n = atoi(c);
			else
				n = index_of(c, field_names, num_fields);
			if (-1 == n) {
				fprintf(stderr, "impossibe index %d\n", n);
				exit(EXIT_FAILURE);
			}
			free(col_types[n]);
			col_types[n] = strdup(TEXT_TYPE);
			c += pmatch[0].rm_eo+1;
		case REG_NOMATCH:
		default:
			break;
		}
	}

	free(fields);
	regfree(&preg);
}

static int file2table(sqlite3 *db, buffered_CSV_t *buf_csv, char * tbl_name,
	struct parameters *params, struct file_params fp)

{
	int run_switches = params->switches;
	char *text_fields = fp.text_fields;
	char *primary_key_fields = fp.primary_key_fields;
	char *foreign_key = fp.foreign_key;
	char *fk_referent = fp.fk_referent;

	// TODO: the first_line_re, etc. should be passed to the ctor of
	// buffered_CSV, which should take care of the skipping and buffering.
	/*
	if (NULL != first_line_re)
		skip_ignored_leading_lines(csv, first_line_re, file_switches); 
	*/

	int num_fields = buf_csv_field_count(buf_csv);
	if (-1 == num_fields) { perror(NULL); exit(EXIT_FAILURE); }

	char **col_names = get_column_names(buf_csv,
			fp.file_switches & fsw_literal_col_names);

	if (run_switches & sw_verbose)
		show_char_array(col_names, num_fields, "col_names");
	if (NULL == col_names) { perror(NULL); exit (EXIT_FAILURE); }

	char **first_data_line_fields = buf_csv_first_data_line_fields(buf_csv);
	char **col_types = get_column_types(first_data_line_fields, num_fields);

	if (NULL != text_fields)
		coerce_to_text(text_fields, col_types, col_names, num_fields);

	create_file_table(db, tbl_name, num_fields, col_names, col_types,
		primary_key_fields, foreign_key, fk_referent, run_switches);

	free_string_array(col_names, num_fields);
	free_string_array(col_types, num_fields);

	return num_fields;
}

// TODO: for consistency, I should stick to either file_index or file_num, but
// not both.

static void read_file_into_table(sqlite3 *db, int file_index,
		struct parameters *params)
{
	int run_switches = params->switches;
	struct file_params fp = params->files[file_index];

	char *index_fields = fp.index_fields;

	/* Analyse file and create appropriate table */

	buffered_CSV_t *buf_csv;
	{
		/* Doing this in a block keeps 'csv' out of scope as soon as we
		 * don't need it anymore. ALl I/O should go through the
		 * buffered_CSV_t struct, not the FILE*. */
		FILE * csv;
		if (0 == strcmp("-", fp.filename))
			csv = stdin;
		else
			csv = fopen(fp.filename, "r");
		if (NULL == csv) die(NULL);

		int flags = 0;
		if (fp.file_switches & fsw_show_skipped_lines) {
			flags |= BUF_CSV_DUMP_SKIPPED;
		}
		if (fp.file_switches & fsw_no_headers)
			flags |= BUF_CSV_NO_HEADER;

		buf_csv = create_buffered_CSV(
				csv, fp.separator, fp.first_line_re, flags);
		if (NULL == buf_csv) die(NULL);
	}

	char * tbl_name;
	if (NULL == fp.alias)
		tbl_name = filename2tablename(fp.filename);
	else
		tbl_name = fp.alias;

	if (NULL == tbl_name) { perror(NULL); exit (EXIT_FAILURE); }

	if (run_switches & sw_verbose) {
		char *istream_name;
		if (0 == strcmp("-", fp.filename))
			istream_name = "stdin";
		else
			istream_name = fp.filename;
		printf("Reading %s into table %s.\n", istream_name, tbl_name);
	}

	int num_fields = file2table(db, buf_csv, tbl_name, params, fp);


	/* Populate table in a transaction */

	start_transaction(db);

	sqlite3_stmt *stmt = NULL;
	stmt = prepare_insert_statement(db, tbl_name, num_fields,
			params->switches);	

	if (! (params->switches & sw_dry_run)) {
		if (NULL == stmt) die (sqlite3_errmsg(db));
		insert_chunk(db, buf_csv, num_fields, WHOLE_FILE, stmt);
	}

 	sqlite3_finalize(stmt);

	stop_transaction(db);

	/* Create index if requested */

	if (NULL != index_fields) 
		create_index(db, tbl_name, index_fields, run_switches); 

	/* Release memory */

	free(tbl_name);
	destroy_buffered_CSV(buf_csv);
}

static sqlite3* create_db(const char *database)
{
	sqlite3 *db;
	char * error_msg = NULL;
	int sql_result = sqlite3_open(database, &db);
	if (SQLITE_OK != sql_result) die (sqlite3_errmsg(db));
	sql_result = sqlite3_exec(
		db, "PRAGMA synchronous = OFF", NULL, NULL, &error_msg);
	if (SQLITE_OK != sql_result) die (error_msg);
	sql_result = sqlite3_exec(
		db, "PRAGMA journal_mode = MEMORY", NULL, NULL, &error_msg);
	if (SQLITE_OK != sql_result) die (error_msg);
	
	return db;
}

static void enable_foreign_keys(sqlite3 *db)
{
	char *error_msg = NULL;

	char *pragma = "PRAGMA foreign_keys=on;";

	int sql_result = sqlite3_exec(db, pragma, NULL, NULL, &error_msg);

	if (SQLITE_OK != sql_result) die(error_msg);
}

static void cleanup(sqlite3 *db, struct parameters *params) 
{
	sqlite3_close(db);
	for (int i = 0; i < params->num_files; i++) {
		free(params->files[i].filename);
		free(params->files[i].first_line_re);
		free(params->files[i].text_fields);
		free(params->files[i].primary_key_fields);
		free(params->files[i].foreign_key);
	}
	free(params->files);
	free(params->user_sql);
	
	free(params);
}

static void regular_run(sqlite3 *db, struct parameters *params)
{

	for (int file_index = 0; file_index < params->num_files; file_index++) 
		read_file_into_table(db, file_index, params);

	if (! (params->switches & sw_dry_run))
		execute_user_query(db, params->user_sql);
}


static void flush_table(sqlite3 *db, char *table_name)
{
	char *error_msg = NULL;	
	char *sql = NULL;
	asprintf(&sql, "DELETE FROM %s;", table_name);
	int result = sqlite3_exec(db, sql, NULL, NULL, &error_msg);
	free(sql);
	if (SQLITE_OK != result) die(error_msg);
}

static void lean_run(sqlite3 *db, struct parameters *params)
{
	int file_index;

	for (file_index = 0; file_index < params->num_files-1; file_index++) 
		read_file_into_table(db, file_index, params);

	struct file_params fp = params->files[file_index];
	// TODO: need to decide if I think in file chunks or in flush periods
	int chunk_size = params->chunk_size;

	buffered_CSV_t *buf_csv;
	{
		FILE * csv;
		if (0 == strcmp("-", fp.filename))
			csv = stdin;
		else
			csv = fopen(fp.filename, "r");
		if (NULL == csv) die(NULL);

		int flags = 0;
		if (fp.file_switches & fsw_show_skipped_lines)
			flags |= BUF_CSV_DUMP_SKIPPED;
		if (fp.file_switches & fsw_no_headers)
			flags |= BUF_CSV_NO_HEADER;

		buf_csv = create_buffered_CSV(
				csv, fp.separator, fp.first_line_re, flags);
		if (NULL == buf_csv) die(NULL);
	}

	char * tbl_name;
	if (NULL == fp.alias)
		tbl_name = filename2tablename(fp.filename);
	else
		tbl_name = fp.alias;

	if (NULL == tbl_name) { perror(NULL); exit (EXIT_FAILURE); }

	if (params->switches & sw_verbose)
		printf("Reading %s into table %s.\n", fp.filename, tbl_name);
	if (params->switches & sw_dry_run) return;

	int num_fields = file2table(db, buf_csv, tbl_name, params, fp);

	sqlite3_stmt *stmt = prepare_insert_statement(db, tbl_name,
			num_fields, params->switches);	
	if (NULL == stmt) die (sqlite3_errmsg(db));

	start_transaction(db);
	do {
		insert_chunk(db, buf_csv, num_fields, chunk_size, stmt);
		execute_user_query(db, params->user_sql);
		flush_table(db, tbl_name);

	} while (! buf_csv_eof(buf_csv));
	stop_transaction(db);
	/* I think it's ok to use buf_csv_eof() here since insert_chunk() will stop
	 * after 'chunk_size' lines have been read (no EOF) OR on EOF. If EOF
	 * is just after a chunk, then the next iteration will return
	 * immediately. */

	sqlite3_finalize(stmt);
}

int main(int argc, char **argv)
{
	struct parameters *params = parse_arguments(argc, argv);

	if (params->switches & sw_verbose) show_params(params);

	sqlite3 *db = create_db(params->database);
	if (params->switches & sw_enable_foreign_keys)
		enable_foreign_keys(db);

	if (WHOLE_FILE == params->chunk_size)
		regular_run(db, params);
	else
		lean_run(db, params);

	cleanup(db, params);

	return 0;
}
