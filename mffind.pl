#!/usr/bin/perl
#
# Code contributed by <spoonm@ghettohackers.net>
#
# This script can be used to find strings (regular expression matches)
# in memfetch dump files in a more useful way than grep could - that is,
# finding exact memory locations and such.
#
# Example
# binary@brothel:~/memfetch$ ./mffind.pl /bin/.*?sh
# [/bin/sh] 1108845 + 0x40024000 -> 0x40132b6d
# [/bin/sh] 1120483 + 0x40024000 -> 0x401358e3
# [/bin/sh] 1121210 + 0x40024000 -> 0x40135bba
# ...
#

die "$0 <regex>\n" if(!@ARGV);
undef $/;
my @files;
open(LISTFILE, '<mfetch.lst') or die "Cannot open mfetch.lst: $!\n";
my $listFile = <LISTFILE>;
while($listFile =~ /\]\s+(.*?):.*?range (.*?) /sg) {
  push(@files, [ $1, $2 ]);
}
close(LISTFILE);

foreach (@files) {
  open(INFILE, "<$_->[0]") or die "Cannot open $_->[0]: $!\n";
  my $file = <INFILE>;
  while($file =~ /($ARGV[0])/sg) {
    my $location = pos($file) - length($1);
    print "[$1] $location + $_->[1] -> ",
      sprintf("%#08x\n", $_->[1] + $location);
  }
  close(INFILE);
}
