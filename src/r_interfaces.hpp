#pragma once

namespace rosetta::detail {
inline constexpr const char* r_s3_interface = R"r(
  api <- environment()
  pointer <- function(object) unclass(object)[[".ptr"]]
  construct <- function(ptr, class) structure(list(.ptr = ptr), class = class)
  define <- function(name, class, method) {
    generic <- eval(substitute(function(object, ...) UseMethod(NAME), list(NAME = name)), envir = api)
    # UseMethod consults the top-level generic registry. The method closures
    # retain api, while the generic itself needs only base R dispatch.
    environment(generic) <- topenv(api)
    assign(name, generic, envir = api)
    registerS3method(name, class, method, envir = api)
    generic
  }
)r";

inline constexpr const char* r_s4_interface = R"r(
  api <- environment()
  definitions <- list()
  pointer <- function(object) methods::slot(object, ".ptr")
  construct <- function(ptr, class) definitions[[class]](.ptr = ptr)
  define <- function(name, class, method) {
    generic <- eval(substitute(function(object, ...) methods::standardGeneric(NAME), list(NAME = name)), envir = api)
    methods::setGeneric(name, generic, signature = "object", where = api)
    methods::setMethod(name, signature = c(object = class), definition = method, where = api)
    get(name, envir = api, inherits = FALSE)
  }
)r";
} // namespace rosetta::detail
