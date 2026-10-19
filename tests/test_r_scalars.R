source(commandArgs(trailingOnly = TRUE)[[3]])
stopifnot(v("int_bits")() == 32) # Verified platform, not a universal C++ width.
for (name in c("char", "schar", "uchar", "short", "ushort", "int", "uint", "long", "ulong", "ll", "ull")) {
  echo <- v(paste0("echo_", name))
  stopifnot(echo(0L) == 0, echo(42) == 42)
  for (bad in list(NULL, numeric(), c(1, 2), NA, NA_integer_, NA_real_, NaN, Inf, -Inf,
                   TRUE, "42", list(42), as.raw(42), 1+0i))
    fails(echo(bad), "expected")
  fails(echo(0.5), "integer")
}
for (name in c("uchar", "ushort", "uint", "ulong", "ull")) fails(v(paste0("echo_", name))(-1), "range")
for (item in list(c("schar", -128, 127), c("uchar", 0, 255), c("short", -32768, 32767),
                  c("ushort", 0, 65535), c("int", -2^31, 2^31-1), c("uint", 0, 2^32-1))) {
  echo <- v(paste0("echo_", item[[1]]))
  lo <- as.numeric(item[[2]]); hi <- as.numeric(item[[3]])
  stopifnot(echo(lo) == lo, echo(hi) == hi)
  fails(echo(lo-1), "range"); fails(echo(hi+1), "range")
}
stopifnot(v("echo_char")(if (v("char_is_signed")()) -128 else 255) %in% c(-128, 255))
stopifnot(v("echo_ll")(-2^53) == -2^53, v("echo_ull")(2^53) == 2^53)
fails(v("echo_ll")(2^53+2), "representable integer")
fails(v("echo_ull")(2^53+2), "representable integer")
stopifnot(v("echo_float")(1.25) == 1.25, v("echo_double")(.Machine$double.xmax) == .Machine$double.xmax)
fails(v("echo_float")(1e39), "range")
fails(v("echo_double")(NaN), "finite")
stopifnot(identical(v("echo_bool")(TRUE), TRUE), identical(v("echo_bool")(FALSE), FALSE))
fails(v("echo_bool")(NA), "TRUE or FALSE")
fails(v("echo_bool")(1), "TRUE or FALSE")
for (text in c("", "ASCII", "世界", "😀", paste(rep("long", 10000), collapse = "")))
  stopifnot(identical(v("echo_string")(text), text))
latin <- rawToChar(as.raw(0xe9)); Encoding(latin) <- "latin1"
stopifnot(identical(v("echo_string")(latin), "é"))
fails(v("echo_string")(NA_character_), "without NA")
stopifnot(v("alias_identity")(42L) == 42, v("defaulted")(12) == 12, is.null(v("do_nothing")()))
fails(v("defaulted")(), "missing") # C++ defaults are not yet R defaults.
fails(v("echo_int")(1, 2), "unused argument")
fails(v("unknown_error")(), "unknown C++ exception")
fails(v("allocation_error")(), "bad_alloc")
fails(v("nonfinite_result")(), "non-finite C++ result")
fails(v("embedded_nul")(), "embedded nul")
# R longjmp and C++ exceptions must both leave the next native call usable.
for (i in 1:20) {
  fails(v("embedded_nul")(), "embedded nul")
  fails(v("unknown_error")(), "unknown C++ exception")
  stopifnot(v("echo_string")("still alive") == "still alive")
}
cat("R scalar, encoding and error contracts passed\n")
