args <- commandArgs(trailingOnly = TRUE)
dyn.load(args[[1]])
source(args[[2]])
v <- function(name) r_verify[[paste0("r_verify__", name)]]
fails <- function(expr, pattern) {
  message <- tryCatch({ force(expr); NULL }, error = conditionMessage)
  if (is.null(message) || !grepl(pattern, message, fixed = TRUE))
    stop("Expected error containing '", pattern, "', got: ", message)
}
