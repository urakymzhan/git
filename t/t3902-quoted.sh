#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='quoted output'

. ./test-lib.sh

FN='濱野'
GN='純'
HT='	'
DQ='"'

test_have_prereq MINGW ||
echo foo 2>/dev/null > "Name and an${HT}HT"
if ! test -f "Name and an${HT}HT"
then
	# FAT/NTFS does not allow tabs in filenames
	skip_all='Your filesystem does not allow tabs in filenames'
	test_done
fi

for_each_name () {
	for name in \
	    Name "Name and a${LF}LF" "Name and an${HT}HT" "Name${DQ}" \
	    "$FN$HT$GN" "$FN$LF$GN" "$FN $GN" "$FN$GN" "$FN$DQ$GN" \
	    "With SP in it" "$FN/file"
	do
		eval "$1" || return 1
	done
}

test_expect_success 'setup' '

	mkdir "$FN" &&
	for_each_name "echo initial >\"\$name\"" &&
	git add . &&
	git commit -q -m Initial &&

	for_each_name "echo second >\"\$name\"" &&
	git commit -a -m Second &&

	for_each_name "echo modified >\"\$name\""

'

# With core.quotepath=true (default), bytes with hi-bit set, in addition to
# controls like \n and \t, are written in octal and the path is enclosed in
# a pair of double-quotes.
#
# With core.quotepath=false, the special case for bytes with hi-bit set is
# disabled.
#
# A SP is treated just like any other bytes, nothing special.
test_expect_success 'setup expected files' '
cat >expect.quoted <<\EOF &&
Name
"Name and a\nLF"
"Name and an\tHT"
"Name\""
With SP in it
"\346\277\261\351\207\216\t\347\264\224"
"\346\277\261\351\207\216\n\347\264\224"
"\346\277\261\351\207\216 \347\264\224"
"\346\277\261\351\207\216\"\347\264\224"
"\346\277\261\351\207\216/file"
"\346\277\261\351\207\216\347\264\224"
EOF

cat >expect.raw <<\EOF
Name
"Name and a\nLF"
"Name and an\tHT"
"Name\""
With SP in it
"濱野\t純"
"濱野\n純"
濱野 純
"濱野\"純"
濱野/file
濱野純
EOF
'

test_expect_success 'check fully quoted output from ls-files' '

	git ls-files >current && test_cmp expect.quoted current

'

test_expect_success 'check fully quoted output from diff-files' '

	git diff --name-only >current &&
	test_cmp expect.quoted current

'

test_expect_success 'check fully quoted output from diff-index' '

	git diff --name-only HEAD >current &&
	test_cmp expect.quoted current

'

test_expect_success 'check fully quoted output from diff-tree' '

	git diff --name-only HEAD^ HEAD >current &&
	test_cmp expect.quoted current

'

test_expect_success 'check fully quoted output from ls-tree' '

	git ls-tree --name-only -r HEAD >current &&
	test_cmp expect.quoted current

'

test_expect_success 'setting core.quotepath' '

	git config --bool core.quotepath false

'

test_expect_success 'check fully quoted output from ls-files' '

	git ls-files >current && test_cmp expect.raw current

'

test_expect_success 'check fully quoted output from diff-files' '

	git diff --name-only >current &&
	test_cmp expect.raw current

'

test_expect_success 'check fully quoted output from diff-index' '

	git diff --name-only HEAD >current &&
	test_cmp expect.raw current

'

test_expect_success 'check fully quoted output from diff-tree' '

	git diff --name-only HEAD^ HEAD >current &&
	test_cmp expect.raw current

'

test_expect_success 'check fully quoted output from ls-tree' '

	git ls-tree --name-only -r HEAD >current &&
	test_cmp expect.raw current

'

test_expect_success 'diff --quote-path-with-sp' '
	git diff --quote-path-with-sp HEAD^ HEAD -- "With*" >actual &&
	sed -e "s/Z$//" >expect <<-\EOF &&
	diff --git "a/With SP in it" "b/With SP in it"
	index e79c5e8..e019be0 100644
	--- "a/With SP in it"	Z
	+++ "b/With SP in it"	Z
	@@ -1 +1 @@
	-initial
	+second
	EOF
	test_cmp expect actual
'

test_done
