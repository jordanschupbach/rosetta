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
  cat("Rosetta R6 demo loaded. Try:\n",
      "  r_demo$r_demo__add(20, 22)\n",
      "  counter <- r_demo$r_demo__Counter$new(20)\n",
      "  counter$add(22)\n",
      "  counter$count <- 10\n",
      "  counter$close()\n",
      "List exports: names(r_demo)\n",
      "Exit: q()\n", sep = "")
})
