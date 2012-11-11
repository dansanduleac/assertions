module compile;

import std.algorithm : map, reduce, joiner;
import std.array;
import std.process : escapeShellCommand;
import std.exception;
import std.stdio;

import env;


class Compilation {

  class ParseError : Exception {
    this(string message) {
      super(message);
    }
  }

  struct Plugin {
    string library_path;
    string plugin_arg_name;
    string plugin_name;
    string[] plugin_args;
    string[] extra_args = [];

    string[] cmdlineArgs() {
      auto toClangFlags = (string arg) {
          return [ "-plugin-arg-" ~ plugin_name, arg ];
      };
      string[] plugin_args_clang =
        joiner(map!toClangFlags (plugin_args)).array;
      return [
        "-load",
        library_path,
        plugin_arg_name,
        plugin_name
      ] ~ plugin_args_clang ~ extra_args;
    }
  }

  static Plugin plugin(string library_path, string name,
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

  this(string filename, string[] extra_args) {
    this.filename = filename;
    base_cmd = [
      buildPath(build_dir, "bin", "clang"),
      "-cc1",
      "-fcolor-diagnostics",
      filename ];
    this.extra_args = extra_args;
  }

  static Compilation parse(string[] args) {
    Compilation c = new Compilation(args[0], args[1..$]);
    return c;
  }

  string[] command(Plugin[] plugins, string[] extras) {
    string[] llvm = emit_ir ? ["-emit-llvm"] : [];
      // ["-o", stripExtension(filename)];

    string[] args = base_cmd ~ llvm;
    foreach (plugin; plugins) {
      args ~= plugin.cmdlineArgs();
    }
    return args ~ extras;
  }

  void run(Plugin[] plugins, string[] extras) {
    auto what = command(plugins, extras);
    auto cmd = escapeShellCommand(what);
    writeln(">>> ", cmd);
    system(cmd);
  }

  private:
  string filename;
  string[] base_cmd;

  bool emit_ir = true;

  string[] extra_args;
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
        c.addedPlugin(buildPath(build_dir, "lib", "AnnotateVariables.so"),
                    "print-fns",
                    c.extra_args)
      ], []);
  /*
  c.run( [
        c.checker(buildPath(build_dir, "lib", "AnnotateVariables.so"),
                     "example.MainCallChecker")
    ], c.extra_args);
  */
}
