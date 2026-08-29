namespace :docs do
  desc "doc/README.md の索引を doc/ の実態から再生成する"
  task :index do
    sh "ruby tool/doc_index.rb"
  end
end
