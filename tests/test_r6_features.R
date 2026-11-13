# R6 backend on the library-shaped example shared with the Python backend.
args <- commandArgs(trailingOnly = TRUE)
dyn.load(args[[1]])
source(args[[2]])
api <- r_features
fails <- function(expr, pattern) {
  message <- tryCatch({ force(expr); NULL }, error = conditionMessage)
  if (is.null(message) || !grepl(pattern, message, fixed = TRUE))
    stop("Expected error containing '", pattern, "', got: ", if (is.null(message)) "no error" else message)
}
check <- function(...) stopifnot(...)

# Classes, inheritance and virtual dispatch.
Circle <- api$Circle; Square <- api$Square
c1 <- Circle$new(2)
check(inherits(c1, "Circle"), inherits(c1, "Shape"), inherits(c1, "RosettaObject"),
      c1$area() == 12, c1$name() == "circle", c1$twice_area() == 24, c1$radius() == 2)
fails(Circle$new(-1), "negative radius")
fails(api$Shape$new(), "abstract")
s <- Square$new()
check(s$side == 1, s$area() == 1)
s$side <- 3
check(s$area() == 9, Square$new(side = 2)$area() == 4)
check(api$area_of(s) == 9, api$describe(c1) == "circle:0")
# Enums cross as enumerator names.
check(identical(c1$kind(), "circle"), identical(api$Kind$custom, "custom"), identical(s$kind(), "square"))
check(api$filled("solid"), !api$filled(api$Fill$None))
fails(api$filled("bogus"), "unknown enumerator")
check(api$Shape$sides_unknown == -1, api$golden == 1.618)

# Most-derived results, ownership transfer and shared ownership.
made <- api$make_shape("circle", 1)
check(inherits(made, "Circle"), made$radius() == 1)
canvas <- api$Canvas$new()
canvas$add(api$make_shape(api$Kind$square, 2))
canvas$share(api$shared_square(3))
transferred <- Circle$new(1)
canvas$add(transferred)
fails(transferred$area(), "closed")
check(canvas$size() == 3, canvas$total() == 4 + 9 + 3)
first <- canvas$at(0)
check(inherits(first, "Square"), first$area() == 4)
shared <- api$shared_square(2)
canvas$share(shared)
check(shared$area() == 4, canvas$total() == 20)
check(api$total_area(list(Circle$new(1), shared)) == 7)

# Overload resolution by R type.
check(api$pick(1L) == "int", api$pick(1.5) == "double", api$pick(2) == "double", api$pick("x") == "string",
      api$pick(c(1, 2)) == "vector", api$pick(s) == "shape")
check(api$add(1) == 111, api$add(1, 2) == 103, api$add(1, 2, 3) == 6, api$add(b = 0, a = 1) == 101)
fails(api$add(1, c = 0), "incompatible arguments")
fails(api$add(), "incompatible arguments")
fails(api$add(1, d = 2), "incompatible arguments")

# Containers.
words <- api$count_words(c("a", "b", "a"))
check(identical(words, c(a = 2L, b = 1L)))
mm <- api$min_max(c(3, 1, 2))
check(mm$first == 1, mm$second == 3, mm[[2]] == 3)
fails(api$min_max(numeric()), "empty input")
check(api$find(c(5L, 7L), 7L) == 1, is.null(api$find(c(5L, 7L), 9L)), api$find(c(5, 7), 7) == 1)
tr <- api$transpose(list(c(1, 2, 3), c(4, 5, 6)))
check(length(tr) == 3, identical(tr[[1]], c(1, 4)))
check(api$byte_sum(as.raw(c(1, 2, 250))) == 253, api$byte_sum(c(1, 2)) == 3)
check(identical(api$bytes_of("AZ"), c(65L, 90L)))
cs <- api$circles(3)
check(is.list(cs), length(cs) == 3, cs[[3]]$radius() == 3)
check(api$repeat_("z", 3) == "zzz", api$initial("hello") == "h")
# Mutable reference parameters update a rosetta_ref(); plain vectors are passed by value.
values <- api$rosetta_ref(c(1, 2))
invisible(api$scale(values, 3))
check(identical(values$value, c(3, 6)))
plain <- c(1, 2)
invisible(api$scale(plain, 3))
check(identical(plain, c(1, 2)))
fails(api$scale(api$rosetta_ref("x"), 2), "expected a numeric")
fails(api$repeat_("z", -1), "negative count")
# Conversion errors name the argument.
fails(api$repeat_("z", 2^40), "argument 'count': number outside C++ type range")
fails(api$scale(api$rosetta_ref("x"), 2), "argument 'values'")

# Callbacks both ways.
check(api$apply(function(x) x * 2, 21) == 42)
fails(api$apply(function(x) stop("inside callback"), 1), "inside callback")
fails(api$apply(function(x) "text", 1), "does not convert")
plus <- api$adder(40)
check(is.function(plus), plus(2) == 42)

# Value type operators, chaining and borrowed references.
Vec2 <- api$Vec2
v <- Vec2$new(1, 2)
w <- v + Vec2$new(3, 4)
check(w$x == 4, w$y == 6, (v * 2)$y == 4, (-v)$x == -1, v == Vec2$new(1, 2), !(v == w), v != w)
check(v[["__getitem__"]](1) == 2, v$inner(Vec2$new(1, 1)) == 3)
same <- v$set_x(10)$set_y(20)
check(identical(same, v), v$x == 10, v$y == 20)
seg <- api$Segment$new()
seg$end <- Vec2$new(3, 4)
check(seg$length2() == 25)
start <- seg$first()
start$x <- 3
check(seg$start$x == 3, seg$length2() == 16)
seg$start$y <- 4
check(seg$length2() == 0)
# C++ clone() keeps its name although R6Class() reserves it.
copied <- Vec2$new(1, 2)$clone()
check(inherits(copied, "Vec2"), copied$y == 2)
# A native function named like an R function still serves plain R calls.
check(api$sum(list(Vec2$new(1, 2), Vec2$new(3, 4))) == 10, api$sum(1:3, 4) == 10, identical(api$sum(), 0L))
held <- api$Segment$new()$first()
invisible(gc())
check(held$x == 0)

# Templates, extensions, qualified names and copy types.
stack <- api$IntStack$new()
stack$push(1L); stack$push(2)
check(stack$size() == 2, identical(stack$items(), c(1L, 2L)), stack$pop() == 2)
check(api$clamp_int(42) == 10, api$clamp_int(3, 5) == 3, api$clamp_real(2.5) == 2.5)
check(c1$label() == "#circle", c1$label("@") == "@circle", Circle$unit()$radius() == 1)
check(api$features__legacy__version() == 2, api$features__legacy__Tag$new()$id == 7,
      identical(api$features__legacy__Mode$off, "off"))
# using-declarations re-export bound entities under the using namespace's name.
check(identical(api$features__legacy__Circle, Circle), identical(api$features__legacy__Kind, api$Kind),
      identical(api$features__legacy__Fill, api$Fill), Circle$new(2)$diameter() == 4,
      api$features__legacy__area_of(Square$new()) == 1)
ledger <- api$Ledger$new()
copy <- ledger$latest()
copy$value <- 5
check(ledger$stamp$value == 1)
aim <- api$Aim$new()
aim$origin$x <- 2
check(aim$target$point$x == 2)
fails(aim$target$point <- Vec2$new(), "Readonly")

# Errors, closing and garbage collection.
fails(api$area_of(1), "wrong native object type")
fails(c1$area(1), "incompatible arguments")
x <- Circle$new(1)
x$close()
fails(x$area(), "closed")
fails(Circle$new(1)$initialize(2), "already initialized")
for (i in 1:200) { tmp <- api$make_shape("square", i); stopifnot(tmp$area() == i * i) }
invisible(gc())
gctorture(TRUE)
tryCatch({
  z <- Vec2$new(1, 1) + Vec2$new(2, 2)
  stopifnot(z$x == 3, api$count_words(c("a"))[["a"]] == 1)
}, finally = gctorture(FALSE))

# Directors: R6 subclasses implement C++ virtual methods.
Triangle <- R6::R6Class("Triangle", inherit = api$Shape, public = list(
  base = NULL, height = NULL,
  initialize = function(base, height) { super$initialize(); self$base <- base; self$height <- height },
  area = function() self$base * self$height / 2,
  name = function() "triangle"
))
t <- Triangle$new(4, 3)
check(inherits(t, "Shape"), t$area() == 6, api$area_of(t) == 6, t$twice_area() == 12,
      api$describe(t) == "triangle:7", api$total_area(list(t, Circle$new(1), Square$new(2))) == 13,
      t$label("@") == "@triangle")
Incomplete <- R6::R6Class("Incomplete", inherit = api$Shape)
fails(api$area_of(Incomplete$new()), "pure virtual method")
Failing <- R6::R6Class("Failing", inherit = api$Shape, public = list(area = function() stop("from R")))
fails(api$area_of(Failing$new()), "from R")
Wrong <- R6::R6Class("Wrong", inherit = api$Shape, public = list(area = function() "not a number"))
fails(api$area_of(Wrong$new()), "does not convert")
# super$ calls the native implementation rather than dispatching back to the override.
Loud <- R6::R6Class("Loud", inherit = Circle, public = list(name = function() toupper(super$name())))
check(api$describe(Loud$new(1)) == "CIRCLE:0", Loud$new(2)$area() == 12)
# Ownership transfer keeps the R implementation alive; the same R object comes back.
Unit <- R6::R6Class("Unit", inherit = api$Shape, public = list(area = function() 1))
canvas <- api$Canvas$new()
unit <- Unit$new()
canvas$add(unit)
check(unit$area() == 1)
rm(unit); invisible(gc())
canvas$share(Square$new(2))
check(canvas$total() == 5, canvas$size() == 2, inherits(canvas$at(0), "Unit"))
same <- canvas$at(0)
check(identical(same, canvas$at(0)))
canvas$clear(); rm(same); invisible(gc())
shared_unit <- Unit$new()
canvas$share(shared_unit)
rm(shared_unit); invisible(gc())
check(canvas$total() == 1)
canvas$clear(); invisible(gc())
# Callback interfaces with a native default implementation.
Summer <- R6::R6Class("Summer", inherit = api$Visitor, public = list(
  seen = NULL,
  visit = function(value) value * 10,
  finished = function(values) self$seen <- values
))
summer <- Summer$new()
check(api$run_visitor(summer, c(1, 2)) == 30, identical(summer$seen, c(1, 2)),
      api$run_visitor(api$Visitor$new(), c(1, 2)) == 3)
gctorture(TRUE)
tryCatch(check(api$area_of(Triangle$new(2, 2)) == 2), finally = gctorture(FALSE))
# Doxygen comments become roxygen blocks (help pages once a package build runs roxygen2).
generated <- readLines(args[[2]])
has <- function(line) any(generated == line)
check(has("#' Adds three integers, Socha & Dorigo style."), has("#' add(a, b = 10, c = 100)"),
      has("#' @param a `integer`. first term"), has("#' @return `integer`: the sum"),
      has("#' * `std::out_of_range`: never, in practice"), has("#' @section Examples (C++):"),
      has("#' defaults(kind = \"square\", label = \"x\", loud = FALSE, weight = 0.5, mark = \"*\", lambda = -2)"),
      has("#' Escapes \\{braces\\}, \\[brackets\\], an @@ sign and"), has("#' \\> 0 at a line start."), has("#' Within \\[`label`, `mark`\\] and (`x`)."),
      has("#' `Circle$new(radius)`"), has("#' * `radius` (`numeric`): distance from the centre; must not be negative"),
      has("#' * `$x` (`numeric`): Horizontal component."), has("#' @rdname Circle"),
      has("#' @param values `numeric vector`"), has("#' @param shapes `list of Shape`"),
      has("#' @format A list of enumerator names, which arguments of this type accept: `\"circle\"`, `\"square\"`, `\"custom\"`."))
# Policy modules: features::data functions live in the list `data`, not at the top level.
check(is.list(api$data), setequal(names(api$data), c("unit", "scaled", "repeat_")))
u <- api$data$unit()
check(inherits(u, "Vec2"), u$x == 1, u$y == 0)
check(api$data$scaled(3L) == 6, api$data$scaled(2.5) == 6.25)
check(api$data$repeat_("ab") == "abab", api$repeat_("x", 3) == "xxx")
check(is.null(api$unit), is.null(api$scaled))
check(has("#' Functions of the C++ namespace `features::data`"), has("`data` <- list("),
      any(startsWith(generated, "#'   \\item{`data$scaled(value)`}{An overloaded submodule function.}")))
cat("R6 features passed\n")
