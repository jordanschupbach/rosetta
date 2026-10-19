# Load exact build artifacts instead of relying on a user gem/load path.
require File.expand_path(ARGV.fetch(0))
require File.expand_path(ARGV.fetch(1))
ARGV.clear
Counter = RubyDemo::R_demo__Counter
Series = RubyVerify::R_verify__Series
Worklist = RubyVerify::R_verify__Worklist
AsyncTask = RubyVerify::R_verify__AsyncTask
puts 'Rosetta Ruby extensions loaded as RubyDemo and RubyVerify.'
puts 'Try: RubyDemo.r_demo__add(20, 22)'
puts 'Classes: Counter.open(20) { |counter| counter.add(22) }'
puts 'Features: Series.open { |series| series.scaled_sum(2) }'
puts 'List exports: RubyDemo.singleton_methods, RubyVerify.constants. Exit with exit or Ctrl-D.'
if $PROGRAM_NAME == __FILE__
  require 'irb'
  IRB.start(__FILE__)
end
