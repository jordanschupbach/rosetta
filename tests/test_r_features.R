source(commandArgs(trailingOnly = TRUE)[[3]])
stopifnot(v("maximum")(20, 42) == 42, v("template_box")(42) == 42,
          v("variadic_sum")(10, 20, 12) == 42, v("constant_answer")() == 42,
          v("constrained_sum")(20, 22) == 42, v("hierarchy_value")(42) == 42,
          v("hierarchy_crosscast")(), v("rosetta_r_c_identity")(42) == 42)
fails(v("constrained_sum")(2^31-1, 1), "overflow")
fails(v("variadic_sum")(2^31-1, 1, 1), "overflow")
Series <- v("Series"); Worklist <- v("Worklist"); Task <- v("AsyncTask")
fails(Series$new(0, -1), "series count")
s <- Series$new(20, 2)
stopifnot(inherits(s, "R6"), s$size() == 2, s$get(0) == 20, s$scaled_sum(2) == 82,
          s$text() == "[20, 21]", s$live() == 1)
s$set(1, 22)
stopifnot(s$scaled_sum(1) == 42)
foreign <- Worklist$new(1)
original <- s$.__enclos_env__$private$.ptr
s$.__enclos_env__$private$.ptr <- foreign$.__enclos_env__$private$.ptr
fails(s$get(0), "wrong native object type")
s$.__enclos_env__$private$.ptr <- original
rm(original)
foreign$close()
fails(s$get(2), "out of range")
fails(s$set(-1, 1), "out of range")
fails(s$scaled_sum(2^31-1), "overflow")
s$close(); s$close()
fails(s$get(0), "native object")
for (order in 1:4) {
  q <- Worklist$new(order)
  q$push("a"); q$push("z"); q$push("m")
  stopifnot(q$size() == 3, q$live() == 1)
  actual <- c(q$pop(), q$pop(), q$pop())
  expected <- list(c("m", "z", "a"), c("a", "z", "m"), c("z", "m", "a"), c("a", "m", "z"))[[order]]
  stopifnot(identical(actual, expected), q$size() == 0)
  fails(q$pop(), "empty")
  q$close()
}
fails(Worklist$new(0), "unknown worklist order")
task <- Task$new(21, 0, FALSE)
stopifnot(task$wait(2000), task$result() == 42, task$result() == 42)
task$close()
task <- Task$new(1, 0, TRUE)
stopifnot(task$wait(2000))
fails(task$result(), "async worker failure")
task$close()
task <- Task$new(1, 60000, FALSE)
stopifnot(!task$ready(), !task$wait(0), task$cancel(), task$wait(2000))
fails(task$result(), "cancelled")
task$close()
fails(Task$new(1, -1, FALSE), "delay")
# Keep closed probes for static live counters while the new owners go through GC.
series_probe <- Series$new(0, 0); series_probe$close()
worklist_probe <- Worklist$new(1); worklist_probe$close()
# GC must cancel and join a pending worker without waiting for its 60s delay.
task <- Task$new(1, 60000, FALSE)
q <- Worklist$new(2); s <- Series$new(1, 3)
rm(task, q, s); invisible(gc())
probe <- Task$new(21, 0, FALSE)
stopifnot(probe$wait(2000), probe$live() == 1, probe$workers() == 0)
probe$close()
# Live counters are exposed as static methods; use closed instances as probes.
stopifnot(probe$live() == 0, series_probe$live() == 0, worklist_probe$live() == 0)
cat("R native template, hierarchy, operator, container and async facades passed\n")
