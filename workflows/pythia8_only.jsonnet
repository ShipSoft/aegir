local lib = import 'lib.libsonnet';
local events = std.parseInt(std.extVar('events'));
{
  stage: lib.stage,
  driver: lib.driver(events),
  sources: {
    pythia8: lib.pythia8,
  },
  modules: {
    output: lib.mc_only_output('pythia8_only_output.root', 'pythia8_only_validation.root', events),
  },
}
