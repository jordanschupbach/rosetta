build = ENV.fetch('ROSETTA_README_BUILD')
require File.join(build, 'ruby_demo.so')
require File.join(build, 'ruby_verify.so')
Counter = RubyDemo::R_demo__Counter
Series = RubyVerify::R_verify__Series
Worklist = RubyVerify::R_verify__Worklist
AsyncTask = RubyVerify::R_verify__AsyncTask
load ARGV.shift
