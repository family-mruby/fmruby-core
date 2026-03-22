#!/usr/bin/env ruby
# web_server.rb - Simple HTTP server for FamilyMruby Web Bluetooth File Manager
#
# Usage:
#   ruby web_server.rb [port]
#
# Default port is 8080. Access at http://localhost:8080
# Web Bluetooth requires localhost or HTTPS.
# No external gems required.

require 'socket'
require 'optparse'

port = 8080
bind = '0.0.0.0'

OptionParser.new do |opts|
  opts.banner = "Usage: #{$0} [options]"
  opts.on('-p', '--port PORT', Integer, "Port number (default: #{port})") { |v| port = v }
  opts.on('-b', '--bind ADDR', "Bind address (default: #{bind})") { |v| bind = v }
end.parse!

# Also accept port as positional argument
port = ARGV[0].to_i if ARGV[0] && ARGV[0] =~ /^\d+$/

DOC_ROOT = File.join(__dir__, 'web')

unless File.directory?(DOC_ROOT)
  abort "Error: #{DOC_ROOT} not found. Run this script from the tool/ directory."
end

MIME_TYPES = {
  '.html' => 'text/html; charset=utf-8',
  '.htm'  => 'text/html; charset=utf-8',
  '.css'  => 'text/css; charset=utf-8',
  '.js'   => 'application/javascript; charset=utf-8',
  '.json' => 'application/json; charset=utf-8',
  '.png'  => 'image/png',
  '.jpg'  => 'image/jpeg',
  '.jpeg' => 'image/jpeg',
  '.gif'  => 'image/gif',
  '.svg'  => 'image/svg+xml',
  '.ico'  => 'image/x-icon',
  '.txt'  => 'text/plain; charset=utf-8',
}.freeze

def mime_type(path)
  ext = File.extname(path).downcase
  MIME_TYPES[ext] || 'application/octet-stream'
end

def serve(client)
  request_line = client.gets
  return unless request_line

  method, raw_path, = request_line.split(' ', 3)
  # Read and discard headers
  while (line = client.gets) && line != "\r\n"; end

  return unless method == 'GET'

  # Decode percent-encoded path and strip query string
  path = URI.decode_www_form_component(raw_path.split('?', 2)[0])
  path = '/index.html' if path == '/'

  # Prevent directory traversal
  safe_path = File.expand_path(File.join(DOC_ROOT, path))
  unless safe_path.start_with?(DOC_ROOT)
    client.print "HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n"
    return
  end

  if File.file?(safe_path)
    body = File.binread(safe_path)
    client.print "HTTP/1.1 200 OK\r\n"
    client.print "Content-Type: #{mime_type(safe_path)}\r\n"
    client.print "Content-Length: #{body.bytesize}\r\n"
    client.print "Connection: close\r\n"
    client.print "\r\n"
    client.write body
  else
    client.print "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n"
  end
rescue => e
  $stderr.puts "Error handling request: #{e.message}"
end

require 'uri'

server = TCPServer.new(bind, port)

puts "FamilyMruby Web File Manager"
puts "  http://localhost:#{port}/"
puts "  Document root: #{DOC_ROOT}"
puts "  Press Ctrl+C to stop"

loop do
  client = server.accept
  Thread.new(client) do |c|
    begin
      serve(c)
    ensure
      c.close rescue nil
    end
  end
rescue Interrupt
  break
end

server.close
puts "\nServer stopped."
