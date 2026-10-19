args <- commandArgs(trailingOnly = TRUE)
Sys.setenv(ROSETTA_R_MODULE = args[[1]], ROSETTA_R_INTERFACE = args[[2]],
           ROSETTA_R_VERIFY_MODULE = args[[4]], ROSETTA_R_VERIFY_INTERFACE = args[[5]], ROSETTA_R_INTERFACES = args[[6]])
source(args[[3]])
stopifnot(r_demo$r_demo__add(20, 22) == 42, r_verify$r_verify__maximum(20, 42) == 42)
counter <- r_demo$r_demo__Counter$new(20)
stopifnot(counter$add(22) == 42)
counter$close()
for (system in list(r_s3, r_s4)) {
  C <- system$demo$r_demo__Counter
  x <- C$new(20)
  stopifnot(C$add(x, 22) == 42)
  C$close(x)
  stopifnot(system$verify$r_verify__maximum(20, 42) == 42)
}
series <- r_verify$r_verify__Series$new(20, 2)
stopifnot(series$text() == "[20, 21]")
series$close()
# R must use registered addresses; string-based native lookup stays disabled.
dll <- getLoadedDLLs()[["r_verify"]]
routines <- getDLLRegisteredRoutines(dll)$.Call
stopifnot(length(routines) > 20, routines[[1]]$numParameters == 1)
message <- tryCatch({ .Call(names(routines)[[1]], 1, PACKAGE = "r_verify"); NULL }, error = conditionMessage)
stopifnot(!is.null(message), r_demo$r_demo__live() == 0)
cat("R REPL loading, module isolation and routine registration passed\n")
