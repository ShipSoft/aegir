local lib = import 'lib.libsonnet';
function(events=100) {
  stage: lib.stage,
  driver: lib.driver(events),
  sources: {
    gun: lib.gun,
  },
  modules: {
    output: lib.mc_only_output('gun_output.root', 'gun_validation.root', events),
  },
}
