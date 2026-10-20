"""Run a MEP chunk inside its selected Nix shell, with native modules loaded."""
import fcntl
import os
from pathlib import Path
import subprocess
import tempfile
import sys

ROOT = Path(__file__).resolve().parents[2]
BACKENDS = {"lua": "EXAMPLES", **{name: f"{name.upper()}_EXAMPLES" for name in ("r", "python", "ruby", "octave", "julia", "java", "csharp", "fortran")}}
TARGETS = {
    "lua": ["rosetta_demo"],
    **{name: [f"rosetta_{name}_demo", f"rosetta_{name}_verify"] for name in ("r", "python", "ruby", "octave", "julia", "java", "csharp", "fortran")},
}
TARGETS["csharp"] = ["rosetta_csharp_demo", "rosetta_csharp_verify"]
TARGETS["java"] = ["rosetta_java_classes"]
BOOTSTRAPS = {
    "lua": ["lua", "bootstrap.lua"],
    "r": ["Rscript", "--vanilla", "bootstrap.R"],
    "python": ["python3", "-I", "bootstrap.py"],
    "ruby": ["ruby", "bootstrap.rb"],
    "octave": ["octave-cli", "--no-gui", "--no-init-file", "--no-site-file", "--no-history", "--quiet", "bootstrap.m"],
    "julia": ["julia", "--startup-file=no", "--history-file=no", "bootstrap.jl"],
}


def run(language, source, arguments):
    source = str(Path(source).resolve(strict=True))
    os.chdir(ROOT)
    if language == "sh":
        return subprocess.call(["bash", "-e", source, *arguments])
    if language not in BACKENDS:
        raise ValueError(f"unsupported README language: {language}")
    directory = ROOT / "build" / "readme" / language
    directory.mkdir(parents=True, exist_ok=True)
    # The lock protects concurrent chunk builds. It is released before the
    # program runs, so long-running examples do not hold up other chunks.
    with (directory / ".build.lock").open("w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        options = [f"-DROSETTA_BUILD_{option}={'ON' if name == language else 'OFF'}" for name, option in BACKENDS.items()]
        if language == "lua":
            options.append("-DROSETTA_BUILD_MODULE_EXAMPLE=OFF")
        subprocess.run([
            "cmake", "-S", str(ROOT), "-B", str(directory), "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_CXX_COMPILER=clang++",
            "-DBUILD_TESTING=OFF", *options,
        ], check=True, stdout=sys.stderr)
        subprocess.run(["cmake", "--build", str(directory), "--parallel", "2", "--target", *TARGETS[language]],
                       check=True, stdout=sys.stderr)
    environment = dict(os.environ, ROSETTA_README_BUILD=str(directory))
    if language == "julia":
        environment["JULIA_DEPOT_PATH"] = str(directory / "julia-depot")
    if language == "java":
        # MEP wraps the Java chunk body in class Main (main=yes). Compile it
        # against the generated API, then launch a JVM with the JNI path set.
        with tempfile.TemporaryDirectory(prefix="rosetta-readme-java-") as temporary:
            main = Path(temporary) / "Main.java"
            main.write_bytes(Path(source).read_bytes())
            classes = directory / "RosettaExamples.jar"
            subprocess.run(["javac", "--release", "17", "-Xlint:all", "-Werror", "-cp", str(classes),
                            "-d", temporary, str(main)], check=True, stdout=sys.stderr)
            return subprocess.call(["java", "-Xcheck:jni", f"-Djava.library.path={directory}", "-cp",
                                    os.pathsep.join((temporary, str(classes))), "Main", *arguments], env=environment)
    if language == "fortran":
        with tempfile.TemporaryDirectory(prefix="rosetta-readme-fortran-") as temporary:
            program = Path(temporary) / "Chunk.f90"
            program.write_bytes(Path(source).read_bytes())
            executable = Path(temporary) / "chunk"
            subprocess.run([
                "gfortran", "-std=f2018", "-Wall", "-Wextra", "-Werror", "-fcheck=all",
                "-I", str(directory / "fortran-modules"), "-J", temporary, str(program),
                str(directory / "librosetta_fortran_demo.a"), str(directory / "librosetta_fortran_verify.a"),
                "-L", str(directory), "-lrosetta_fortran_demo_native", "-lrosetta_fortran_verify_native",
                f"-Wl,-rpath,{directory}", "-o", str(executable),
            ], check=True, stdout=sys.stderr)
            return subprocess.call([str(executable), *arguments], env=environment)
    if language == "csharp":
        # Compile top-level statements with the generated API. Framework packs
        # come from the SDK; clearing sources keeps execution independent of NuGet.
        import xml.etree.ElementTree as xml
        with tempfile.TemporaryDirectory(prefix="rosetta-readme-csharp-") as temporary:
            project = xml.Element("Project", Sdk="Microsoft.NET.Sdk")
            properties = xml.SubElement(project, "PropertyGroup")
            for key, value in {"OutputType": "Exe", "TargetFramework": "net10.0", "ImplicitUsings": "enable",
                               "Nullable": "enable", "TreatWarningsAsErrors": "true"}.items():
                xml.SubElement(properties, key).text = value
            items = xml.SubElement(project, "ItemGroup")
            for name in ("CSharpDemo", "CSharpVerify"):
                xml.SubElement(items, "Compile", Include=str(directory / "generated" / "csharp" / (name + ".cs")))
            xml.ElementTree(project).write(Path(temporary) / "Chunk.csproj", encoding="unicode")
            Path(temporary, "Program.cs").write_bytes(Path(source).read_bytes())
            Path(temporary, "NuGet.Config").write_text("<configuration><packageSources><clear /></packageSources></configuration>")
            environment.update(DOTNET_CLI_HOME=str(directory / "dotnet-home"), DOTNET_CLI_TELEMETRY_OPTOUT="1", DOTNET_NOLOGO="1")
            output = Path(temporary) / "out"
            subprocess.run(["dotnet", "build", str(Path(temporary) / "Chunk.csproj"), "--output", str(output),
                            "--nologo", "--disable-build-servers"], check=True, stdout=sys.stderr, env=environment)
            import shutil
            for library in (directory / "csharp").glob("*CSharp*.*"):
                if library.suffix in (".so", ".dylib"):
                    shutil.copy2(library, output / library.name)
            return subprocess.call(["dotnet", str(output / "Chunk.dll"), *arguments], env=environment)
    command = [str(ROOT / "scripts" / "readme" / item) if item.startswith("bootstrap.") else item for item in BOOTSTRAPS[language]]
    return subprocess.call([*command, source, *arguments], env=environment)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit("usage: run.py LANGUAGE CHUNK [ARGUMENTS...]")
    try:
        sys.exit(run(sys.argv[1], sys.argv[2], sys.argv[3:]))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
