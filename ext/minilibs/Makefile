default: test_regexp.exe test_tree.exe test_xml.exe

CFLAGS := -Wall -Wextra -pedantic -g

test_regexp.exe: regexp.c regexp.h
	$(CC) $(CFLAGS) -DTEST -o $@ regexp.c

test_tree.exe: tree.c tree.h
	$(CC) $(CFLAGS) -DTEST -o $@ tree.c

test_xml.exe: xml.c xml.h
	$(CC) $(CFLAGS) -DTEST -o $@ xml.c

clean:
	$(RM) -f test_xml.exe test_regexp.exe
