build <- Sys.getenv("ROSETTA_README_BUILD")
stopifnot(nzchar(build))
interfaces <- file.path(build, "generated", "r")
for (name in c("r_demo", "r_verify")) {
    dyn.load(file.path(build, paste0(name, .Platform$dynlib.ext)))
    source(file.path(interfaces, paste0(name, ".R")), local = .GlobalEnv)
}
for (system in c("s3", "s4")) {
    target <- new.env(parent = globalenv())
    for (name in c("r_demo", "r_verify"))
        source(file.path(interfaces, system, paste0(name, ".R")), local = target)
    assign(paste0("r_", system), list(demo = target$r_demo, verify = target$r_verify), envir = .GlobalEnv)
}
source(commandArgs(trailingOnly = TRUE)[1], local = .GlobalEnv, print.eval = TRUE)
