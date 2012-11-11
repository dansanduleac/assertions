module env;

public import std.process;
public import std.path;

string build_dir;

static this() {
  build_dir = environment["HOME"] ~ "/projects/TESLA/build/build";
}
