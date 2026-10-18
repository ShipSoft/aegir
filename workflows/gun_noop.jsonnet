local lib = import 'lib.libsonnet';
{
  stage: lib.stage,
  driver: lib.driver(std.parseInt(std.extVar('events'))),
  sources: {
    gun: lib.gun,
  },
  modules: {
    output: lib.noop_output,
  },
}
