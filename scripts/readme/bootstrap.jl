rosetta_build = ENV["ROSETTA_README_BUILD"]
include(joinpath(rosetta_build, "julia_demo.jl"))
include(joinpath(rosetta_build, "julia_verify.jl"))
const verify = julia_verify
const Series = verify.r_verify__Series
const Worklist = verify.r_verify__Worklist
const AsyncTask = verify.r_verify__AsyncTask
include(abspath(popfirst!(ARGS)))
