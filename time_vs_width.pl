#!/usr/bin/perl
use warnings;
use strict;

my $nruns=20;

for my $wp (7..13) {
	my $w=1<<$wp;
	for my $nth (128) {
		#print STDERR "scons -s -f SConstruct sel=2 v=0 nruns=$nruns nth=$nth w=$w\n";
		system("scons -s ref=0 sel=2 v=0 w=$w nth=$nth nruns=$nruns");
		my $res=`./vm ./test_oclgprm_matmult.tdc64 16`;
		print "$w\t$res\n";
	}
}
