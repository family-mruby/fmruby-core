# frozen_string_literal: true
#
# tree_sync.rb - copy files and directory trees, writing only what changed.
#
# rake setup lays our gems and patches over the picoruby submodule and into
# flash/ on every build. A plain `cp -rf` (or rm -rf + cp) gives every copy a
# new modification time, so ninja and the host mruby build (and the checkpoint
# table and CMake's configure behind them) took ~50 unchanged C files for
# changed and rebuilt them on every build. Here a destination file is
# written only when its content differs from the source, so an unchanged file
# keeps its time and nothing downstream is rebuilt.
#
#   FmrbTreeSync.file(src, dst)              one file (cp -f)
#   FmrbTreeSync.overlay(src_dir, dst_dir)   a tree over another (cp -rf src/. dst/)
#   FmrbTreeSync.mirror(dst_dir, map, keep:) dst becomes exactly the map:
#       map is { "relative/path/in/dst" => "source file or directory" }; files
#       of dst that the map does not name are deleted (rm -rf + cp), except
#       the ones keep: names (relative paths or globs, File.fnmatch with
#       FNM_PATHNAME, a directory name keeps everything under it).
#   FmrbTreeSync.write(path, content)         write a generated file if changed
#
# Each returns the number of files written or deleted, so a caller can say
# whether anything happened. The source's permission bits travel with a
# written file (cp -rf did that too).
require "fileutils"

module FmrbTreeSync
  module_function

  def same?(a, b)
    File.file?(b) && File.size(a) == File.size(b) && FileUtils.compare_file(a, b)
  end

  # One file. Replaces a directory or a symlink at dst.
  def file(src, dst)
    raise ArgumentError, "tree_sync: #{src} is not a file" unless File.file?(src)
    return 0 if !File.symlink?(dst) && same?(src, dst)
    FileUtils.rm_rf(dst) if File.directory?(dst) || File.symlink?(dst)
    FileUtils.mkdir_p(File.dirname(dst))
    tmp = "#{dst}.tree_sync.#{$$}"
    FileUtils.cp(src, tmp)
    File.chmod(File.stat(src).mode & 0o7777, tmp)
    File.rename(tmp, dst)
    1
  end

  # A generated file's content (a String), written only when it differs.
  def write(path, content)
    return 0 if File.file?(path) && File.binread(path) == content
    FileUtils.mkdir_p(File.dirname(path))
    tmp = "#{path}.tree_sync.#{$$}"
    File.binwrite(tmp, content)
    File.rename(tmp, path)
    1
  end

  # The files under a directory, relative to it (dot files included, the
  # entries "." and ".." not). A symlink to a file counts as a file.
  def files_under(dir)
    Dir.glob("**/*", File::FNM_DOTMATCH, base: dir)
       .reject { |rel| %w[. ..].include?(File.basename(rel)) }
       .select { |rel| File.file?(File.join(dir, rel)) }
  end

  # The { dst_rel => src_file } of a map whose values may be directories.
  def expand(map)
    out = {}
    map.each do |rel, src|
      if File.directory?(src)
        files_under(src).each { |f| out[rel.empty? ? f : File.join(rel, f)] = File.join(src, f) }
      elsif File.file?(src)
        out[rel] = src
      else
        raise ArgumentError, "tree_sync: #{src} does not exist"
      end
    end
    out
  end

  def overlay(src_dir, dst_dir)
    expand("" => src_dir).sum { |rel, src| file(src, File.join(dst_dir, rel)) }
  end

  def kept?(rel, keep)
    keep.any? do |pat|
      rel == pat || rel.start_with?("#{pat}/") ||
        File.fnmatch?(pat, rel, File::FNM_PATHNAME | File::FNM_DOTMATCH)
    end
  end

  def mirror(dst_dir, map, keep: [])
    want = expand(map)
    n = want.sum { |rel, src| file(src, File.join(dst_dir, rel)) }
    return n unless File.directory?(dst_dir)
    files_under(dst_dir).each do |rel|
      next if want.key?(rel) || kept?(rel, keep)
      File.delete(File.join(dst_dir, rel))
      n += 1
    end
    # Symlinks that point nowhere (files_under skips them) and the
    # directories left empty go too.
    Dir.glob("**/*", File::FNM_DOTMATCH, base: dst_dir).each do |rel|
      next if %w[. ..].include?(File.basename(rel)) || kept?(rel, keep)
      p = File.join(dst_dir, rel)
      if File.symlink?(p) && !File.exist?(p)
        File.delete(p)
        n += 1
      end
    end
    Dir.glob("**/*/", File::FNM_DOTMATCH, base: dst_dir)
       .map { |d| d.chomp("/") }
       .reject { |d| %w[. ..].include?(File.basename(d)) }
       .sort_by { |d| -d.count("/") }
       .each do |rel|
      p = File.join(dst_dir, rel)
      Dir.rmdir(p) if File.directory?(p) && !File.symlink?(p) && Dir.empty?(p) && !kept?(rel, keep)
    end
    n
  end
end
