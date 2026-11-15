# FmrbFile: whole files by path (sig/fmrb_file.rbs, doc/app_boundary W4b-3).
# Everything is the app boundary's (C, registered by the gem) but list, the
# wrapper that makes list_raw's lines an Array.
module FmrbFile
  def self.list(dir)
    s = list_raw(dir)
    return nil unless s
    s.split("\n")
  end
end
