addpath(getenv('ROSETTA_README_BUILD'));
demo = octave_demo();
verify = octave_verify();
Counter = demo.r_demo__Counter;
Series = verify.r_verify__Series;
Worklist = verify.r_verify__Worklist;
AsyncTask = verify.r_verify__AsyncTask;
rosetta_chunk_args = argv();
source(rosetta_chunk_args{1});
