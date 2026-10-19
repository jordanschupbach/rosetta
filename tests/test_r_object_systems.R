args <- commandArgs(trailingOnly = TRUE)
dyn.load(args[[1]]); source(args[[2]])
dyn.load(args[[3]]); source(args[[4]])
system <- args[[5]]
fails <- function(expr, pattern = "") {
  message <- tryCatch({ force(expr); NULL }, error = conditionMessage)
  if (is.null(message) || !grepl(pattern, message, fixed = TRUE))
    stop("Expected error containing '", pattern, "', got: ", message)
}
Counter <- r_demo$r_demo__Counter
x <- Counter$new(20)
stopifnot(is.object(x), identical(isS4(x), system == "s4"),
          Counter$add(x, 22) == 42, Counter$value(x) == 42,
          Counter$get_count(x) == 42, Counter$get_initial(x) == 20,
          Counter$answer() == 42, Counter$live() == 1,
          identical(Counter$value, r_demo$r_demo__Counter__value))
if (system == "s3") {
  stopifnot(inherits(x, Counter$class), is.function(getS3method(
    "r_demo__r_demo__Counter__value", Counter$class, envir = environment(Counter$new))))
  derived <- x
  class(derived) <- c("UserCounter", class(derived))
} else {
  stopifnot(methods::is(x, Counter$class), methods::validObject(x), methods::is(Counter$value, "standardGeneric"))
  derived_class <- methods::setClass(paste0(Counter$class, "_User"), contains = Counter$class,
                                     where = environment(Counter$value))
  derived <- derived_class(.ptr = methods::slot(x, ".ptr"))
  # A bare S4 allocation cannot bypass native construction and validation.
  empty <- methods::new(Counter$class)
  fails(Counter$value(empty), "native object")
}
stopifnot(Counter$value(derived) == 42)
alias <- x
Counter$set_count(alias, 10)
Counter$set_label(x, "计数器")
stopifnot(Counter$value(x) == 10, Counter$get_label(alias) == "计数器")
fails(Counter$set_initial(x, 1), "Readonly")
fails(Counter$add(x, NA), "without NA")
fails(Counter$value(42))
fails(Counter$new(-1), "negative initial value")
stopifnot(Counter$live() == 1)
restored <- unserialize(serialize(x, NULL))
fails(Counter$value(restored), "closed or unserialized")
Counter$close(restored)
stopifnot(Counter$value(x) == 10)
Counter$close(alias); Counter$close(alias)
fails(Counter$value(x), "closed or unserialized")
fails(Counter$value(derived), "closed or unserialized")
stopifnot(Counter$live() == 0)
rm(x, alias, derived, restored); invisible(gc())
# Reloading the interface preserves existing native owners and old generics.
x <- Counter$new(9)
reloaded <- new.env(parent = globalenv())
source(args[[2]], local = reloaded)
stopifnot(Counter$value(x) == 9, reloaded$r_demo$r_demo__Counter$value(x) == 9)
Counter$close(x)
x <- Counter$new(1)
rm(x); invisible(gc())
stopifnot(Counter$live() == 0)

Series <- r_verify$r_verify__Series
Worklist <- r_verify$r_verify__Worklist
Task <- r_verify$r_verify__AsyncTask
s <- Series$new(20, 2)
stopifnot(Series$get(s, 0) == 20, Series$scaled_sum(s, 2) == 82, Series$text(s) == "[20, 21]")
Series$set(s, 1, 22)
stopifnot(Series$scaled_sum(s, 1) == 42)
fails(Series$get(s, 2), "out of range")
fails(Counter$value(s))
ptr <- if (system == "s3") s[[".ptr"]] else methods::slot(s, ".ptr")
forged <- if (system == "s3") structure(list(.ptr = ptr), class = Counter$class) else methods::new(Counter$class, .ptr = ptr)
fails(Counter$value(forged), "wrong native object type")
rm(forged, ptr)
Series$close(s)
for (order in 1:4) {
  q <- Worklist$new(order)
  Worklist$push(q, "a"); Worklist$push(q, "z"); Worklist$push(q, "m")
  values <- c(Worklist$pop(q), Worklist$pop(q), Worklist$pop(q))
  expected <- list(c("m", "z", "a"), c("a", "z", "m"), c("z", "m", "a"), c("a", "m", "z"))[[order]]
  stopifnot(identical(values, expected), Worklist$size(q) == 0)
  fails(Worklist$pop(q), "empty")
  Worklist$close(q)
}
task <- Task$new(21, 0, FALSE)
stopifnot(Task$wait(task, 2000), Task$result(task) == 42, Task$result(task) == 42)
Task$close(task)
task <- Task$new(1, 0, TRUE)
stopifnot(Task$wait(task, 2000))
fails(Task$result(task), "async worker failure")
Task$close(task)
task <- Task$new(1, 60000, FALSE)
stopifnot(Task$cancel(task), Task$wait(task, 2000))
fails(Task$result(task), "cancelled")
Task$close(task)
task <- Task$new(1, 60000, FALSE)
q <- Worklist$new(2); s <- Series$new(1, 2)
rm(task, q, s); invisible(gc())
stopifnot(Task$live() == 0, Task$workers() == 0, Series$live() == 0, Worklist$live() == 0)
gctorture(TRUE)
tryCatch({
  x <- Counter$new(42)
  stopifnot(Counter$value(x) == 42)
  Counter$close(x)
}, finally = gctorture(FALSE))
cat(system, "dispatch, aliases, fields, ownership, serialization, native facades and GC passed\n")
