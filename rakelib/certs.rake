# rakelib/certs.rake
# The CA bundle the ESP32 terminals trust for HTTPS (doc/ca_bundle/plan.md):
# ESP-IDF's "full" bundle minus config/cacrt_exclude.txt.
#
#   rake certs:bundle       # rebuild config/cacrt_fmrb.pem from the IDF in the build image (docker)
#   rake certs:test         # host tests of the tool and the committed bundle
#   rake certs:check_build  # compare the bundle an ESP32 build generated with config/cacrt_fmrb.pem

namespace :certs do
  desc "Rebuild config/cacrt_fmrb.pem: IDF's full CA bundle minus config/cacrt_exclude.txt (docker)"
  task :bundle do
    sh "ruby", File.join(ROOT_DIR, "tools", "cacrt_bundle.rb"), "--image", IMAGE
  end

  desc "CA bundle tests: the tool and config/cacrt_fmrb.pem (host Ruby, no docker)"
  task :test do
    sh "ruby", File.join(ROOT_DIR, "test", "ca_bundle", "run.rb")
  end

  desc "Check the x509_crt_bundle of the last ESP32 build against config/cacrt_fmrb.pem"
  task :check_build do
    bin = File.join(ROOT_DIR, "build", "esp-idf", "mbedtls", "x509_crt_bundle")
    abort "#{bin} not found: rake build:esp32 first" unless File.exist?(bin)
    sh "ruby", File.join(ROOT_DIR, "tools", "cacrt_bundle.rb"), "--check-bin", bin
  end
end
