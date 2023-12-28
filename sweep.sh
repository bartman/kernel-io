#!/bin/bash
set -e

mkdir -p reports
rm -f output.csv

for th in 1 2 ; do
	#for rd in 0 1 25 33 50 66 75 99 100 ; do
	for rd in 100 99 75 66 50 33 25 1 0 ; do
		for or in 0 1 ; do
			for ((qd=1; qd<255; qd*=2)) ; do
				for ((bu=1; bu<255; bu*=2)) ; do
					for bf in 0 1 ; do
						for ((run=0; run<2; run++)) ; do
							# run once and discard, run again and capture
							( set -e -x
							./kio.py -c config.yaml \
								--label run$run \
								--num-threads $th \
								--runtime 5 \
								--burst-finish $bf \
								--offset-random $or \
								--read-burst $bu \
								--write-burst $bu \
								--read-mix-percent $rd \
								--queue-depth $qd \
								--output-yaml reports/report-th$th-qd$qd-rd$rd-rb$bu-wb$bu-bf$bf-or$or-run$run.yaml \
								--output-csv output.csv
							)
						done
					done
				done
			done
		done
		# sleep when switching read/write mix or random/sequential
		sleep 30
	done
done
