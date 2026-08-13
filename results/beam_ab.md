# Task 2 -- beam search construction: REJECTED

Command:
  ./giscup solve --data data/GIS-cup-sample-dataset.geojson --tau T --k K \
      --radius 1000 --verify-radius 3000 --lns-sec 60 --swap 400 \
      --algo beam+lns --beam B

Correctness gate (giscup exact, k<=3) -- beam holds ratio 1.00, same as baseline:
  n=40 tau=0.50 k=3 -> 20 = optimum
  n=40 tau=0.75 k=3 ->  8 = optimum
  n=70 tau=0.50 k=2 -> 15 = optimum
  n=70 tau=0.50 k=3 -> 22 = optimum

Deciding sub-problems (60s polish each, radius 1000):
  tau    k     beam=0   beam=4   beam=8
  0.75   50       384      379      381
  0.50   50       868      861      861
  0.75  500      2868      ...      ...
  runtime         76s     110s     144s   (at tau=0.75 k=50)

Beam loses at every sub-problem measured and costs 45-95% more wall clock.
Default remains OFF (--beam 0).
