# rakelib/help.rake
# What a bare `rake` does. Without a default task it aborts with "Don't know
# how to build task 'default'", which is a poor first impression in a tree
# where the interesting tasks are spread over several rakelib files; print the
# list instead. Same shape as fmruby-graphics-audio and the workspace root.

desc "List available tasks"
task :help do
  sh "rake -T"
end

task :default => :help
