# FmrbNet.request -- fetching something over the network without stopping.
#
# The blocking Net::HTTP is still there and still fine for a tool that only
# ever runs on a board. This is the one an app uses when it has to work in the
# browser too, and the shape is the same in all three places:
#
#   def on_create
#     @req = FmrbNet.request("https://example.org/thing.json")
#   end
#
#   def on_update
#     if @req && @req.done?
#       @req.ok? ? use(@req.body) : complain(@req.error)
#       @req = nil
#     end
#     50
#   end
#
# Deliberately not a callback. A block that is stored and called later is the
# shape Spinel breaks on when it captures an outer local, which is the same
# trap set_timer has.
#
# The difference between the environments is where the waiting happens, not
# what the app writes. In the browser nothing waits: the page fetches while
# the machine carries on drawing. On a board the request is made inside
# `request` and `done?` is true straight away -- that stops the app's own task
# for as long as the server takes, exactly as Net::HTTP does today, and the
# rest of the machine keeps running because those tasks are preemptive. The
# browser cannot do that: its tasks are cooperative, so one that waits stops
# the screen with it.
module FmrbNet
  module RequestState
    PENDING = 0
    DONE    = 1
    FAILED  = 2
    UNKNOWN = 3
  end

  class Request
    attr_reader :url, :status, :body, :error

    def initialize(url)
      @url = url
      @status = nil
      @body = nil
      @error = nil
      @handle = nil
      @done = false
      # Bare constants inside a class are looked up as FmrbNet::Request::X in
      # picoruby, so every one of them is written from the top.
      if ::FmrbConst::BOARD == "wasm"
        start_in_page
      else
        fetch_now
      end
    end

    def done?
      return true if @done
      return true unless @handle
      state = ::FmrbNet._fetch_poll(@handle)
      return false if state == ::FmrbNet::RequestState::PENDING
      if state == ::FmrbNet::RequestState::DONE
        @status = ::FmrbNet._fetch_status(@handle)
        @body = ::FmrbNet._fetch_body(@handle)
        @error = "no answer from #{@url}" if @status == 0
      elsif state == ::FmrbNet::RequestState::FAILED
        @error = ::FmrbNet._fetch_error(@handle)
        @error = "could not reach #{@url}" if @error.nil? || @error.empty?
      else
        @error = "the request was lost"
      end
      release
      @done = true
      true
    end

    def ok?
      return false unless done?
      return false if @error
      !@status.nil? && @status >= 200 && @status < 300
    end

    # Stop caring about the answer. Safe to call more than once.
    def cancel
      release
      @done = true
      @error = "cancelled" unless @error
      nil
    end

    # Not part of the API; picoruby has no visibility control, so the name is
    # the only thing marking these as internal.
    def release
      return unless @handle
      ::FmrbNet._fetch_free(@handle)
      @handle = nil
    end

    def start_in_page
      @handle = ::FmrbNet._fetch_start(@url)
      return if @handle
      @error = "the page would not take the request"
      @done = true
    end

    # Boards and the simulator: do it here and be finished. The task blocks;
    # the machine does not.
    def fetch_now
      @done = true
      begin
        res = ::Net::HTTP.get_response(::URI.parse(@url))
        @status = res.code.to_i
        @body = res.body
      rescue => e
        @error = e.message
      end
      nil
    end
  end

  # Start fetching. Returns straight away with something to ask later; it is
  # never the answer itself, which is why this is not called get.
  def self.request(url)
    ::FmrbNet::Request.new(url)
  end
end
