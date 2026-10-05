local lib = import 'lib.libsonnet';
{
  stage: lib.stage,
  driver: lib.driver(100),
  sources: {
    gun: lib.gun,
  },
  modules: {
    output: lib.mc_only_output('gun_output.root', 'gun_validation.root'),
  },
}
