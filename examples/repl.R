# R reads this user profile before entering the interactive session.
local({
  module <- Sys.getenv("ROSETTA_R_MODULE")
  interface <- Sys.getenv("ROSETTA_R_INTERFACE")
  if (!nzchar(module) || !nzchar(interface))
    stop("Expected ROSETTA_R_MODULE and ROSETTA_R_INTERFACE from the r-repl target")
  dyn.load(module)
  source(interface, local = .GlobalEnv)
  verification <- Sys.getenv("ROSETTA_R_VERIFY_MODULE")
  if (nzchar(verification)) {
    dyn.load(verification)
    source(Sys.getenv("ROSETTA_R_VERIFY_INTERFACE"), local = .GlobalEnv)
    cat("Feature verification loaded as r_verify. Try r_verify$r_verify__maximum(20, 42)\n")
  }
  interfaces <- Sys.getenv("ROSETTA_R_INTERFACES")
  if (nzchar(interfaces) && nzchar(verification)) {
    for (system in c("s3", "s4")) {
      target <- new.env(parent = globalenv())
      source(file.path(interfaces, system, "r_demo.R"), local = target)
      source(file.path(interfaces, system, "r_verify.R"), local = target)
      assign(paste0("r_", system), list(demo = target$r_demo, verify = target$r_verify), envir = .GlobalEnv)
    }
    cat("S3 and S4 loaded as r_s3 and r_s4. Try:\n",
        "  C <- r_s3$demo$r_demo__Counter; x <- C$new(20); C$add(x, 22); C$close(x)\n",
        "  C <- r_s4$demo$r_demo__Counter; x <- C$new(20); C$add(x, 22); C$close(x)\n", sep = "")
  }
  cat("Rosetta R6 demo loaded. Try:\n",
      "  r_demo$r_demo__add(20, 22)\n",
      "  counter <- r_demo$r_demo__Counter$new(20)\n",
      "  counter$add(22)\n",
      "  counter$count <- 10\n",
      "  counter$close()\n",
      "List exports: names(r_demo)\n",
      "Exit: q()\n", sep = "")
})
