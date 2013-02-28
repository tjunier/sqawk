#!/bin/bash

# NOTE: the tests are roughly, but not completely, in order of increasing task
# complexity (and following the order in which features were added).

shopt -o -s nounset
#shopt -o -s xtrace

SQAWK=./sqawk
DATA_DIR=data

sample=$DATA_DIR/sample.csv

# Test 1: just print out some info about what would be done

cat <<END > test1.exp
dry run:	T
verbose:	T
show generated SQL:	F
database:	:memory:

1 file(s):
	data/sample.csv, not indexed, separated by TAB, no lines skipped.

user SQL:	SELECT count(*) FROM sample
Reading data/sample.csv into table sample.
col_names[0]: num
col_names[1]: class
col_names[2]: date
col_names[3]: field
col_names[4]: label
col_types[0]: NUMERIC
col_types[1]: NUMERIC
col_types[2]: TEXT
col_types[3]: TEXT
col_types[4]: TEXT
END

echo -n "Test  1:	"
if $SQAWK -n -v $sample 'SELECT count(*) FROM sample' > test1.out ; then
	if diff test1.out test1.exp ; then
		echo "pass"
		rm test1.{out,exp}
	else
		echo "FAIL"
	fi
else
	echo "ERROR"
fi


# Test 2: count of records

cat <<END > test2.exp
count(*)
2372
END

echo -n "Test  2:	"
if $SQAWK $sample 'SELECT count(*) FROM sample' > test2.out ; then
	if diff test2.out test2.exp ; then
		echo "pass"
		rm test2.{out,exp}
	else
		echo "FAIL"
	fi
else
	echo "ERROR"
fi


# Test 3: SELECT *, first 5 rows

cat <<END > test3.exp
num	class	date	field	label
1	1	2002-07-22	"56"	"Boldness"
1	7	2007-07-01	"6513"	"Toddler"
2	9	2000-07-01	"6525"	"Toddler"
3	2	2000-07-01	"6523"	"Toddler"
4	11	2006-11-15	"6667"	"Toddler"
END

echo -n "Test  3:	"
if $SQAWK $sample 'SELECT * FROM sample LIMIT 5' > test3.out ; then
	if diff test3.out test3.exp ; then
		echo "pass"
		rm test3.{out,exp}
	else
		echo "FAIL"
	fi
else
	echo "ERROR"
fi

# Test 4: SELECT *, first 5 rows

cat <<END > test4.exp
num	class	date	field	label
31	18	2005-12-27	"69082"	"Frog"
32	35	2005-01-23	"69084"	"Frog"
33	38	2006-11-11	"69091"	"Frog"
END

echo -n "Test  4:	"
if $SQAWK $sample 'SELECT * FROM sample WHERE num > 30 LIMIT 3' > test4.out ; then
	if diff test4.out test4.exp ; then
		echo "pass"
		rm test4.{out,exp}
	else
		echo "FAIL"
	fi
else
	echo "ERROR"
fi

# Test 5: -k keeps database

SQAWK_DB=sqawk.db

[ -f $SQAWK_DB ] && rm -f $SQAWK_DB

echo -n "Test  5:	"
if $SQAWK -k $sample 'SELECT * FROM sample WHERE num > 30 LIMIT 3' > test5.out ; then
	if [ -f $SQAWK_DB ] ; then
		echo "pass"
		rm test5.out $SQAWK_DB
	else
		echo "FAIL"
	fi
else
	echo "ERROR"
fi

# Test 6: aggregate function

cat <<END > test6.exp
num	class	date	field	label
149	50	2006-11-15	"62536"	"Regression"
150	90	2008-08-08	"62530"	"Regression"
151	170	2008-04-30	"62658"	"Regression"
152	26	2007-10-09	"62650"	"Regression"
153	51	2002-02-10	"62672"	"Regression"
END

echo -n "Test  6:	"
if $SQAWK $sample 'SELECT * FROM sample WHERE num > (SELECT avg(num) FROM sample) LIMIT 5;' > test6.out ; then
	if diff test6.out test6.exp ; then
		echo "pass"
		rm test6.{out,exp}
	else
		echo "FAIL"
	fi
else
	echo "ERROR"
fi

# Test 7: more than one file, just print what will be done

cat <<END > test7.exp
dry run:	T
verbose:	T
show generated SQL:	F
database:	:memory:

2 file(s):
	data/sample.csv, not indexed, separated by TAB, no lines skipped.
	data/chr21.vcf, not indexed, separated by TAB, skip to ^#CHROM, literal column names.

user SQL:	SELECT count(*) FROM sample
Reading data/sample.csv into table sample.
col_names[0]: num
col_names[1]: class
col_names[2]: date
col_names[3]: field
col_names[4]: label
col_types[0]: NUMERIC
col_types[1]: NUMERIC
col_types[2]: TEXT
col_types[3]: TEXT
col_types[4]: TEXT
Reading data/chr21.vcf into table chr21.
col_names[0]: '#CHROM'
col_names[1]: 'POS'
col_names[2]: 'ID'
col_names[3]: 'REF'
col_names[4]: 'ALT'
col_names[5]: 'QUAL'
col_names[6]: 'FILTER'
col_names[7]: 'INFO'
col_types[0]: NUMERIC
col_types[1]: NUMERIC
col_types[2]: TEXT
col_types[3]: TEXT
col_types[4]: TEXT
col_types[5]: NUMERIC
col_types[6]: TEXT
col_types[7]: TEXT
END

echo -n "Test  7:	"
if $SQAWK -n -v $sample -f '^#CHROM' -l data/chr21.vcf 'SELECT count(*) FROM sample' > test7.out ; then
	if diff test7.out test7.exp ; then
		echo "pass"
		rm test7.{out,exp}
	else
		echo "FAIL"
	fi
else
	echo "ERROR"
fi

# Test 8: two files, simple where clause

cat <<END > composers.csv
Id	Name	Born	Died	Period
023	J. S. Bach	1685	1750	02
156	W. A. Mozart	1756	1791	03
980	A. Vivaldi	1678	1741	02
004	J. Lennon	1940	1980	05
END

cat <<END > periods.csv
Id	Name	Start	End
02	Baroque	1600	1750
03	Classical	1750	1800
04	Romantic	1801	1900
05	Pop	1960	1980
END

cat <<END > test8.exp
Composer	Period
J. S. Bach	Baroque
W. A. Mozart	Classical
A. Vivaldi	Baroque
J. Lennon	Pop
END

echo -n "Test  8:	"
if $SQAWK composers.csv periods.csv 'SELECT c.Name AS Composer, p.Name AS Period FROM composers as c, periods as p WHERE c.Period = p.Id' > test8.out ; then
	if diff test8.out test8.exp ; then
		echo 'pass'
		rm test8.{out,exp} composers.csv periods.csv
	else
		echo 'FAIL'
	fi
else
	echo "ERROR"
fi

# Test 9: joins

cat <<END > department.csv
DepartmentID	DepartmentName
31	Sales
33	Engineering
34	Clerical
35	Marketing
END

cat <<END > employee.csv
LastName	DepartmentID
Rafferty	31
Jones	33
Steinberg	33
Robinson	34
Smith	34
John	NULL
END

cat <<END > test9.1.exp
LastName	DepartmentID	DepartmentID	DepartmentName
Rafferty	31	31	Sales
Jones	33	33	Engineering
Steinberg	33	33	Engineering
Robinson	34	34	Clerical
Smith	34	34	Clerical
END

echo -n "Test  9:	"
if $SQAWK department.csv employee.csv 'SELECT * FROM employee, department WHERE employee.DepartmentID = department.DepartmentID' > test9.1.out ; then
	if diff test9.1.out test9.1.exp ; then
		echo 'pass'
		rm test9.1.{out,exp}
	else
		echo 'FAIL'
	fi
else
	echo "ERROR"
fi

# TODO: now do the other types of joins...

rm employee.csv department.csv

# Test 10: alternative separator

cat <<END > test10.csv
id;stratum_no;event_date;cohort_id;cohort
37;1;2007-12-18;"1076";"AHOD"
9;1;2007-12-18;"238";"AHOD"
23;1;2007-12-18;"625";"AHOD"
32;1;2007-12-18;"846";"AHOD"
47;2;2009-02-27;"1373";"AHOD"
END

cat <<END > test10.exp
id	stratum_no	event_date	cohort_id	cohort
37	1	2007-12-18	"1076"	"AHOD"
9	1	2007-12-18	"238"	"AHOD"
23	1	2007-12-18	"625"	"AHOD"
32	1	2007-12-18	"846"	"AHOD"
47	2	2009-02-27	"1373"	"AHOD"
END

echo -n "Test 10:	"
if $SQAWK -s ';' test10.csv 'SELECT * FROM test10' > test10.out ; then
	if diff test10.out test10.exp ; then
		echo "pass"
		rm test10.{out,exp,csv}
	else
		echo "FAIL"
	fi
else
	echo "ERROR"
fi

# TODO (later) use data/forets.csv & al, to check behaviour with accented
# chars, atc.

# Test 11: simple index

cat <<END > test11.csv
id	stratum_no	event_date	cohort_id	cohort
37	1	2007-12-18	"1076"	"AHOD"
9	1	2007-12-18	"238"	"AHOD"
23	1	2007-12-18	"625"	"AHOD"
32	1	2007-12-18	"846"	"AHOD"
47	2	2009-02-27	"1373"	"AHOD"
END

cat <<END > test11.exp
seq	name	unique
0	test11:id	0
END

echo -n "Test 11:	"
if $SQAWK -i id test11.csv 'PRAGMA index_list (test11)' > test11.out ; then
	if diff test11.out test11.exp ; then
		echo 'pass'
		rm test11.{csv,out,exp}
	else
		echo 'FAIL'
	fi
else
	echo 'ERROR'
fi

# Test 12: nameless fields

cat <<END > test12.csv
37	1	2007-12-18	"1076"	"AHOD"
9	1	2007-12-18	"238"	"AHOD"
23	1	2007-12-18	"625"	"AHOD"
32	1	2007-12-18	"846"	"AHOD"
47	2	2009-02-27	"1373"	"AHOD"
END

cat <<END > test12.exp
f1	f2	f3	f4	f5
37	1	2007-12-18	"1076"	"AHOD"
9	1	2007-12-18	"238"	"AHOD"
23	1	2007-12-18	"625"	"AHOD"
32	1	2007-12-18	"846"	"AHOD"
47	2	2009-02-27	"1373"	"AHOD"
END

echo -n "Test 12:	"
if $SQAWK -H test12.csv 'SELECT * FROM test12' > test12.out ; then
	if diff test12.out test12.exp ; then
		echo 'pass'
		rm test12.{csv,out,exp}
	else
		echo 'FAIL'
	fi
else
	echo 'ERROR'
fi

# Test 13: join using an index.

echo -n "Test 13:	"
if $SQAWK -i '"#CHROM",POS,REF' -f '^#CHROM' -l data/chr21.vcf \
	-f '#CHROM' -l data/chr21b.vcf \
	'SELECT chr21.* FROM chr21 JOIN chr21b USING ("#CHROM",POS,REF) LIMIT 5' \
	> test13.out ; then
	if diff test13.out data/test13.exp ; then
		echo 'pass'
		rm test13.out
	else
		echo 'FAIL'
	fi
else
	echo 'ERROR'
fi

# Test 14: show SQL

cat <<END > composers.csv
Id	Name	Born	Died	Period
023	J. S. Bach	1685	1750	02
156	W. A. Mozart	1756	1791	03
980	A. Vivaldi	1678	1741	02
004	J. Lennon	1940	1980	05
END

cat <<END > periods.csv
Id	Name	Start	End
02	Baroque	1600	1750
03	Classical	1750	1800
04	Romantic	1801	1900
05	Pop	1960	1980
END

cat <<END > test14.exp
-- Create table:
CREATE TABLE composers (Id NUMERIC, Name TEXT, Born NUMERIC, Died NUMERIC, Period NUMERIC);
-- Insert data ('?': SQLite C API placeholders):
INSERT INTO composers VALUES (?, ?, ?, ?, ?)
-- Create table:
CREATE TABLE periods (Id NUMERIC, Name TEXT, Start NUMERIC, End NUMERIC);
-- Insert data ('?': SQLite C API placeholders):
INSERT INTO periods VALUES (?, ?, ?, ?)
-- Create index:
CREATE INDEX 'periods:Id' ON periods (Id)
END

echo -n "Test 14:	"
if $SQAWK -q -n composers.csv -i Id periods.csv '' > test14.out ; then
	if diff test14.out test14.exp ; then
		echo 'pass'
		rm composers.csv periods.csv test14.{exp,out}
	else
		echo 'FAIL'
	fi
else
	echo 'ERROR'
fi

# test 15: unicify, preserving order

cat <<END > test15.csv
Genus	species
Zelkova	sicula
Carpinus	betulus
Alnus	incana
Quercus	suber
Betula	utilis
Zelkova	sicula
Betula	utilis
Carpinus	betulus
Zelkova	sicula
END

cat <<END > test15.exp
Genus	species
Zelkova	sicula
Carpinus	betulus
Alnus	incana
Quercus	suber
Betula	utilis
END

echo -n "Test 15:	"
if $SQAWK test15.csv 'SELECT DISTINCT * FROM test15 GROUP BY (rowid)' > test15.out ; then
	if diff test15.out test15.exp ; then
		echo 'pass'
		rm test15.{exp,out,csv}
	else
		echo 'FAIL'
	fi
else
	echo 'ERROR'
fi

# test 16: user-supplied pattern for 1st line

cat <<END > test16.vcf
## some dummy VCF headers
## another blabla line
#CHROM	POS	ID	REF	ALT	QUAL	FILTER	INFO	FORMAT
21	9413228	.	C	G,X	11.2	.	DP=5;AF1=1;CI95=0.1667,1;DP4=1,1,1,2;MQ=15;FQ=-24.8;PV4=1,0.2,1,1	GT:PL:GQ
21	9414083	.	T	A,X	26.1	.	DP=2;AF1=1;CI95=0.3333,1;DP4=0,0,1,1;MQ=33;FQ=-28.9	GT:PL:GQ
21	9414283	.	T	C,X	21.8	.	DP=2;AF1=1;CI95=0.1667,1;DP4=0,0,2,0;MQ=29;FQ=-27.5	GT:PL:GQ
21	9414326	.	A	G,X	21.8	.	DP=2;AF1=1;CI95=0.1667,1;DP4=0,0,2,0;MQ=29;FQ=-27.5	GT:PL:GQ
21	9415019	.	A	C,X	56.2	.	DP=3;AF1=1;CI95=0.3333,1;DP4=0,0,0,3;MQ=32;FQ=-29.8	GT:PL:GQ
21	9415869	.	A	T,X	39.4	.	DP=3;AF1=1;CI95=0.3333,1;DP4=0,0,3,0;MQ=28;FQ=-29.8	GT:PL:GQ
21	9416219	.	G	A,X	72.7	.	DP=4;AF1=1;CI95=0.5,1;DP4=0,0,3,1;MQ=29;FQ=-32.1	GT:PL:GQ
21	9416257	.	C	G,X	72.7	.	DP=4;AF1=1;CI95=0.5,1;DP4=0,0,3,1;MQ=29;FQ=-32.1	GT:PL:GQ
21	9416338	.	T	G,X	8.83	.	DP=4;AF1=0.3904;CI95=0.1667,0.6667;DP4=0,2,2,0;MQ=38;FQ=10.6;PV4=0.33,1,0.1,0.092	GT:PL:GQ
END

cat <<END > test16.exp
CHROM	POS	ID	REF	ALT	QUAL	FILTER	INFO	FORMAT
21	9413228	.	C	G,X	11.2	.	DP=5;AF1=1;CI95=0.1667,1;DP4=1,1,1,2;MQ=15;FQ=-24.8;PV4=1,0.2,1,1	GT:PL:GQ
21	9414083	.	T	A,X	26.1	.	DP=2;AF1=1;CI95=0.3333,1;DP4=0,0,1,1;MQ=33;FQ=-28.9	GT:PL:GQ
21	9414283	.	T	C,X	21.8	.	DP=2;AF1=1;CI95=0.1667,1;DP4=0,0,2,0;MQ=29;FQ=-27.5	GT:PL:GQ
21	9414326	.	A	G,X	21.8	.	DP=2;AF1=1;CI95=0.1667,1;DP4=0,0,2,0;MQ=29;FQ=-27.5	GT:PL:GQ
21	9415019	.	A	C,X	56.2	.	DP=3;AF1=1;CI95=0.3333,1;DP4=0,0,0,3;MQ=32;FQ=-29.8	GT:PL:GQ
21	9415869	.	A	T,X	39.4	.	DP=3;AF1=1;CI95=0.3333,1;DP4=0,0,3,0;MQ=28;FQ=-29.8	GT:PL:GQ
21	9416219	.	G	A,X	72.7	.	DP=4;AF1=1;CI95=0.5,1;DP4=0,0,3,1;MQ=29;FQ=-32.1	GT:PL:GQ
21	9416257	.	C	G,X	72.7	.	DP=4;AF1=1;CI95=0.5,1;DP4=0,0,3,1;MQ=29;FQ=-32.1	GT:PL:GQ
21	9416338	.	T	G,X	8.83	.	DP=4;AF1=0.3904;CI95=0.1667,0.6667;DP4=0,2,2,0;MQ=38;FQ=10.6;PV4=0.33,1,0.1,0.092	GT:PL:GQ
END

echo -n "Test 16:	"
if $SQAWK -f '^#CHROM' test16.vcf 'SELECT * FROM test16' > test16.out ; then
	if diff test16.out test16.exp ; then
		echo 'pass'
		rm test16.{vcf,out,exp}
	else
		echo 'FAIL'
	fi
else
	echo 'ERROR'
fi

# test 17: literal field names

cat <<END > test17.csv
#V1	s p a c e	w3&i%rd	23i
45.5	tuz	67	gagöm
12.3	baa	53	gagueum
END

cat <<END > test17.exp
#V1	s p a c e	w3&i%rd	23i
45.5	tuz	67	gagöm
12.3	baa	53	gagueum
END

echo -n "Test 17:	"
if $SQAWK -l test17.csv 'SELECT * FROM test17' > test17.out ; then
	if diff test17.out test17.exp ; then
		echo 'pass'
		rm test17.{csv,out,exp}
	else
		echo 'FAIL'
	fi
else
	echo 'ERROR'
fi

# Test 18: join using an index, and allow extra header lines in the first file
# (-F).

echo -n "Test 18:	"
if $SQAWK -i '"#CHROM",POS,REF' -F '^#CHROM' -l data/chr21.vcf \
	-f '^#CHROM' -l data/chr21b.vcf \
	'SELECT chr21.* FROM chr21 JOIN chr21b USING ("#CHROM",POS,REF) LIMIT 5' \
	> test18.out ; then
	if diff test18.out data/test18.exp ; then
		echo 'pass'
		rm test18.out
	else
		echo 'FAIL'
	fi
else
	echo 'ERROR'
fi

# Test 19: numbers in scientific notation -> as numeric, not text
# TODO: both columns must be NUMERIC (or better, FLOAT)

cat <<END > test19.csv
int	float	sci	scip
4	3.14159	1.602E-19	6.02E+23
END

cat <<END > test19.exp
dry run:	F
verbose:	T
show generated SQL:	F
database:	:memory:

1 file(s):
	test19.csv, not indexed, separated by TAB, no lines skipped.

user SQL:	PRAGMA table_info(test19)
Reading test19.csv into table test19.
col_names[0]: int
col_names[1]: float
col_names[2]: sci
col_names[3]: scip
col_types[0]: NUMERIC
col_types[1]: NUMERIC
col_types[2]: NUMERIC
col_types[3]: NUMERIC
cid	name	type	notnull	dflt_value	pk
0	int	NUMERIC	0	(null)	0
1	float	NUMERIC	0	(null)	0
2	sci	NUMERIC	0	(null)	0
3	scip	NUMERIC	0	(null)	0
END

echo -n "Test 19:	"
if $SQAWK -v test19.csv 'PRAGMA table_info(test19)' > test19.out ; then
	if diff test19.out test19.exp ; then
		echo 'pass'
		rm test19.{csv,out,exp}
	else
		echo 'FAIL'
	fi
else
	echo 'ERROR'
fi

# Test 20: force type

cat <<END > test20.csv
foo	bar	baz	quux
12	Glops	0.98	1E-12
2A	Bosz	1.23	6E+3
Q7	Modgorzh	8.00	0.001
END

cat <<END > test20.exp
dry run:	F
verbose:	T
show generated SQL:	F
database:	:memory:

1 file(s):
	test20.csv, not indexed, separated by TAB, no lines skipped, field(s) 'foo,bar' forced to TEXT.

user SQL:	SELECT * FROM test20 WHERE foo = 'Q7'
Reading test20.csv into table test20.
col_names[0]: foo
col_names[1]: bar
col_names[2]: baz
col_names[3]: quux
col_types[0]: TEXT
col_types[1]: TEXT
col_types[2]: NUMERIC
col_types[3]: NUMERIC
foo	bar	baz	quux
Q7	Modgorzh	8	0.001
END

echo -n "Test 20:	"
if $SQAWK -v -t foo,bar test20.csv "SELECT * FROM test20 WHERE foo = 'Q7'" > test20.out ; then
	if diff test20.out test20.exp ; then
		echo 'pass'
		rm test20.{csv,out,exp}
	else
		echo 'FAIL'
	fi
else
	echo 'ERROR'
fi

# Test 21: primary key (in PRAGMA)

cat <<END > people.csv
surname	name	job
BROWN	Jill	conceptual janitor
DOBBE	Isaac	intemporal
ARNE	Harald	gravity tester
KAKKONEN	Inari	relativist chef
KIM	Cheol-su	mineral gardener
END

cat <<END > test21.exp
dry run:	F
verbose:	T
show generated SQL:	F
database:	:memory:

1 file(s):
	people.csv, not indexed, separated by TAB, no lines skipped, PRIMARY KEY name,surname.

user SQL:	PRAGMA table_info(people)
Reading people.csv into table people.
col_names[0]: surname
col_names[1]: name
col_names[2]: job
col_types[0]: TEXT
col_types[1]: TEXT
col_types[2]: TEXT
cid	name	type	notnull	dflt_value	pk
0	surname	TEXT	0	(null)	1
1	name	TEXT	0	(null)	1
2	job	TEXT	0	(null)	0
END

echo -n "Test 21:	"
if $SQAWK -v -p name,surname people.csv 'PRAGMA table_info(people)' > test21.out; then
	if diff test21.out test21.exp ; then
		echo 'pass'
		rm test21.{out,exp} people.csv
	else
		echo 'FAIL'
	fi
else
	echo 'ERROR'
fi

# Test 22: primary key (in a real query)

cat <<END > people.csv
surname	name	job
BROWN	Jill	conceptual janitor
DOBBE	Isaac	intemporal
DOBBE	Candace	human-human interface optimizer
ARNE	Harald	gravity tester
KAKKONEN	Inari	relativist chef
KIM	Cheol-su	mineral gardener
ARNE	Ingrid	stone psychologist
END

# Note that SQLite does not crash or abort: it just skips rows that violate the
# PRIMARY KEY constraint.

cat <<END > test22.exp
surname	name	job
BROWN	Jill	conceptual janitor
DOBBE	Isaac	intemporal
ARNE	Harald	gravity tester
KAKKONEN	Inari	relativist chef
KIM	Cheol-su	mineral gardener
END

echo -n "Test 22:	"
if $SQAWK -p surname people.csv 'SELECT * FROM people' > test22.out; then
	if diff test22.out test22.exp ; then
		echo 'pass'
		rm test22.{out,exp} people.csv
	else
		echo 'FAIL'
	fi
else
	echo 'ERROR'
fi

# Test 23: primary key (in a real query)

cat <<END > people.csv
id	surname	name	jobid
0	BROWN	Jill	4
1	DOBBE	Isaac	6
2	ARNE	Harald	6
3	KAKKONEN	Inari	8
4	KIM	Cheol-su	1
5	ARNE	Ingrid	5
END

cat <<END > jobs.csv
id	job
0	conceptual janitor
1	intemporal
2	gravity tester
3	relativist chef
4	mineral gardener
5	stone psychologist
6	literary technician
7	systems analyst
END

# Note that SQLite does not crash or abort: it just skips rows that violate the
# FOREIGN KEY constraint

cat <<END > test23.exp
dry run:	F
verbose:	T
show generated SQL:	F
database:	:memory:

2 file(s):
	jobs.csv, not indexed, separated by TAB, no lines skipped, PRIMARY KEY id.
	people.csv, not indexed, separated by TAB, no lines skipped, foreign key 'jobid' on 'jobs(id)'.

user SQL:	SELECT * FROM people
Reading jobs.csv into table jobs.
col_names[0]: id
col_names[1]: job
col_types[0]: NUMERIC
col_types[1]: TEXT
Reading people.csv into table people.
col_names[0]: id
col_names[1]: surname
col_names[2]: name
col_names[3]: jobid
col_types[0]: NUMERIC
col_types[1]: TEXT
col_types[2]: TEXT
col_types[3]: NUMERIC
id	surname	name	jobid
0	BROWN	Jill	4
1	DOBBE	Isaac	6
2	ARNE	Harald	6
4	KIM	Cheol-su	1
5	ARNE	Ingrid	5
END

echo -n "Test 23:	"
if $SQAWK -p id jobs.csv -v -K jobid 'jobs(id)' people.csv 'SELECT * FROM people' > test23.out; then
	if diff test23.out test23.exp ; then
		echo 'pass'
		rm test23.{out,exp} people.csv jobs.csv
	else
		echo 'FAIL'
	fi
else
	echo 'ERROR'
fi

# Test 24: flushing - The last file is read in chunks, which are flushed before
# the next chunk is read, after running the SQL on the chunk. 

cat <<END > flush.txt
code	name
3	Bar Karam
5	Bar Kawal
7	Modgorzh
9	Tufulcán
2	Qudbul
6	Crepanarpio
8	Melagum
1234	Qudbar
15	Qudruzh
21	Traxcán
1357	Tatultán
22	Toulabaobou
24	Toulaolao
26	Ukelelola
END

cat <<END > pos.csv
name	numb
Mana	1234
Zora	7890
Aoki	1357
END

cat <<END > test24.exp
name	name
Qudbar	Mana
name	name
Tatultán	Aoki
END

echo -n "Test 24:	"
if $SQAWK -P 5 pos.csv flush.txt 'SELECT flush.name, pos.name  FROM flush JOIN pos ON (code = numb)' > test24.out ; then
	if diff test24.out test24.exp ; then
		echo 'pass'
		rm test24.{out,exp} pos.csv flush.txt
	else
		echo 'FAIL'
	fi
else
	echo 'ERROR'
fi

# Test 25 - reading from stdin

cat <<END > test25.exp
dry run:	F
verbose:	T
show generated SQL:	F
database:	:memory:

1 file(s):
	stdin, not indexed, separated by TAB, no lines skipped.

user SQL:	SELECT * FROM stdin LIMIT 5
Reading stdin into table stdin.
col_names[0]: num
col_names[1]: class
col_names[2]: date
col_names[3]: field
col_names[4]: label
col_types[0]: NUMERIC
col_types[1]: NUMERIC
col_types[2]: TEXT
col_types[3]: TEXT
col_types[4]: TEXT
num	class	date	field	label
1	1	2002-07-22	"56"	"Boldness"
1	7	2007-07-01	"6513"	"Toddler"
2	9	2000-07-01	"6525"	"Toddler"
3	2	2000-07-01	"6523"	"Toddler"
4	11	2006-11-15	"6667"	"Toddler"
END

echo -n "Test 25:	"
if cat $sample | $SQAWK -v - 'SELECT * FROM stdin LIMIT 5' > test25.out ; then
	if diff test25.out test25.exp ; then
		echo "pass"
		rm test25.{out,exp}
	else
		echo "FAIL"
	fi
else
	echo "ERROR"
fi
