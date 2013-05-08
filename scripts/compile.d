module compile;

import std.algorithm : map, reduce, joiner, findSplit;
import std.array;
import std.path : extension;
import std.process : escapeShellCommand;
import std.exception;
import std.stdio;

import env;


class Compilation {

  static class ParseError : Exception {
    this(string message) {
      super(message);
    }
  }

  struct Plugin {
    string library_path;
    // Name of the Xclang arg to load a plugin.
    // e.g. "-add-plugin", or "-analyzer-checker".
    string plugin_arg_name;
    string plugin_name;
    string[] plugin_args;
    string[] extra_args = [];

    string[] cmdlineArgs() {
      string plugin_arg_arg = "-plugin-arg-" ~ plugin_name;
      string[] plugin_args_clang_flags =
        joiner(map!(arg => [plugin_arg_arg, arg]) (plugin_args)).array;
      return [
        "-load",
        library_path,
        plugin_arg_name,
        plugin_name
      ] ~ plugin_args_clang_flags ~ extra_args;
    }
  }

  static Plugin onlyPlugin(string library_path, string name,
                       string[] args = []) {
    Plugin p = {library_path, "-plugin", name, args};
    return p;
  }

  static Plugin addedPlugin(string library_path, string name,
                            string[] args = []) {
    Plugin p = {library_path, "-add-plugin", name, args};
    return p;
  }

  static Plugin checker(string library_path, string name) {
    Plugin p = {library_path, "-analyzer-checker", name, [], ["-analyze"]};
    return p;
  }

  this(string filename, string[] extra_args, string[] cmdline_args) {
    this.filename = filename;
    string std;
    switch (extension(filename)) {
      case ".c"  : std = "-std=c11"; break;
      case ".cpp": std = "-std=c++11"; break;
      default    : throw new Error("Expecting file to be .c or .cpp");
    }
    base_cmd = [
      buildPath(build_dir, "bin", "clang"),
      "-cc1",
      "-fcolor-diagnostics",
      std,
      filename ];
    this.extra_args = extra_args;
    this.cmdline_args = cmdline_args;
  }

  static Compilation parse(string[] args) {
    if (args.length == 0) {
      throw new ParseError("Expecting filename.");
    }
    auto r = findSplit(args[1..$], ["--"]);
    auto extra_args = r[0];
    auto cmdline_args = r[2];
    Compilation c = new Compilation(args[0], extra_args, cmdline_args);
    return c;
  }

  // Automatically incorportes cmdline_args into the mix.
  string[] command(Plugin[] plugins, string[] extras = []) {
    string[] action = emit_ir ? ["-emit-llvm"] : [];
      // ["-o", stripExtension(filename)];

    string[] args = base_cmd ~ action;
    foreach (plugin; plugins) {
      args ~= plugin.cmdlineArgs();
    }
    return args ~ cmdline_args ~ extras;
  }

  void run(Plugin[] plugins, string[] extras = []) {
    auto what = command(plugins, extras);
    // Beware of stupid DMD 2.061 bug here
    // http://d.puremagic.com/issues/show_bug.cgi?id=9307
    auto cmd = escapeShellCommand(what);
    stderr.writeln(">>> ", cmd);
    //system(cmd);
    // Replace this process, make it easy to debug.
    execv(what[0], what);
  }

  private:
  string filename;
  string[] base_cmd;

  // Let the user specify the action for now in ./run
  bool emit_ir = false;

  string[] extra_args;
  string[] cmdline_args = [];
}

void main(string[] args) {
  auto c = Compilation.parse(args[1..$]);
  /*
  c.run([
        c.addedPlugin(buildPath(build_dir, "lib", "PrintFunctionNames.so"),
                    "print-fns",
                    c.extra_args)
      ], []);
  */

  c.run([
        c.onlyPlugin(buildPath(build_dir, "lib", "AnnotateVariables.so"),
                    "annotate-vars",
                    c.extra_args)
      ]);
  /*
  c.run( [
        c.checker(buildPath(build_dir, "lib", "AnnotateVariables.so"),
                     "example.MainCallChecker")
    ], c.extra_args);
  */
}
