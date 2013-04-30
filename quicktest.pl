#!/usr/bin/perl
use warnings;
use strict;

my $nruns=1;
my $nth=128;
if (@ARGV && $ARGV[0] eq 'CPU') {
$nth=1;
}
my $ngroups=16;

my $wp = 10;
	my $w=1<<$wp;
		#print STDERR "scons -s -f SConstruct sel=2 v=0 nruns=$nruns nth=$nth w=$w\n";
		system("scons sel=2 v=1 w=$w nth=$nth nruns=$nruns");
		my $res=`./vm ./test_oclgprm_matmult.tdc64 $ngroups`;
		print "$w\t$nth\t$res";#\t" unless $run==0;
		print "\n";
