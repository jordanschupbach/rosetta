args <- commandArgs(trailingOnly = TRUE)
dyn.load(args[[1]])
source(args[[2]])
fails <- function(expr, pattern) {
  message <- tryCatch({ force(expr); NULL }, error = conditionMessage)
  stopifnot(!is.null(message), grepl(pattern, message, fixed = TRUE))
}
stopifnot(r_demo$r_demo__add(20L, 22) == 42,
          r_demo$r_demo__half(5) == 2.5,
          identical(r_demo$r_demo__negate(TRUE), FALSE),
          r_demo$r_demo__greet("R") == "Hello, R!",
          r_demo$r_demo__greet("世界") == "Hello, 世界!")
for (bad in list(NA, NA_integer_, NA_real_, NaN, Inf, "1", TRUE, c(1, 2), numeric()))
  fails(r_demo$r_demo__add(bad, 1), "expected")
fails(r_demo$r_demo__add(1.5, 1), "integer")
fails(r_demo$r_demo__add(2^40, 1), "range")
fails(r_demo$r_demo__negate(1), "TRUE or FALSE")
fails(r_demo$r_demo__greet(NA_character_), "without NA")
fails(r_demo$r_demo__too_large(), "exact numeric range")
fails(r_demo$r_demo__fail(), "native failure")
fails(r_demo$r_demo__Counter$new(-1), "negative initial value")
invisible(gc())
stopifnot(r_demo$r_demo__live() == 0)
x <- r_demo$r_demo__Counter$new(20)
y <- r_demo$r_demo__Counter$new(1)
stopifnot(inherits(x, "R6"), x$add(22) == 42, x$value() == 42,
          x$count == 42, x$initial == 20, y$value() == 1, x$answer() == 42,
          r_demo$r_demo__live() == 2, is.null(x$clone))
x$count <- 10
x$label <- "计数器"
stopifnot(x$value() == 10, x$label == "计数器")
fails(x$initial <- 1, "Readonly")
fails(x$initialize(30), "already initialized")
stopifnot(x$value() == 10, r_demo$r_demo__live() == 2)
# External pointers lose their native address across serialization.
restored <- unserialize(serialize(x, NULL))
fails(restored$value(), "closed or unserialized")
restored$close()
x$close()
x$close()
fails(x$value(), "wrong native object type")
fails(x$count <- 1, "wrong native object type")
stopifnot(r_demo$r_demo__live() == 1)
rm(x, y, restored)
invisible(gc())
stopifnot(r_demo$r_demo__live() == 0)
for (i in 1:50) {
  z <- r_demo$r_demo__Counter$new(i)
  stopifnot(z$value() == i)
  rm(z)
}
invisible(gc())
stopifnot(r_demo$r_demo__live() == 0)
# Exercise R allocations while native string results and owners are live.
gctorture(TRUE)
tryCatch({
  z <- r_demo$r_demo__Counter$new(3)
  stopifnot(z$value() == 3, r_demo$r_demo__greet("GC") == "Hello, GC!")
  z$close()
}, finally = gctorture(FALSE))
cat("R6 integration passed\n")
