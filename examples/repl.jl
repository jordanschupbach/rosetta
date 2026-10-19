# Run with: just julia-repl
include(joinpath(ARGS[1], "julia_demo.jl"))
include(joinpath(ARGS[1], "julia_verify.jl"))
const demo = julia_demo
const verify = julia_verify
const Counter = demo.r_demo__Counter
const Series = verify.r_verify__Series
const Worklist = verify.r_verify__Worklist
const AsyncTask = verify.r_verify__AsyncTask
println("Rosetta Julia examples loaded: demo, verify, Counter, Series, Worklist, AsyncTask")
println("c = Counter(20); c.add(22); c.count; close(c)")
