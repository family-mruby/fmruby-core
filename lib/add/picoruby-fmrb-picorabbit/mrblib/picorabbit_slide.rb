# PicoRabbit slide data structures
# Compatible with Harucom OS picorabbit format

module PicoRabbit
  class Element
    attr_reader :type, :text, :level
    attr_accessor :align
    # :small, :large or :xlarge from a {:.large} line, nil for the body size.
    attr_accessor :size
    # True from a {:.shadow} line: the text is drawn over a dark copy of
    # itself, offset down and right, so it reads on a picture.
    attr_accessor :shadow
    # The number a :numbered item was written with, so the list reads the way
    # its author numbered it (1. 1. 1. included) rather than by its position.
    attr_accessor :number
    # How wide an :image was asked to be: a width in pixels (![w=200](..))
    # or a percentage of the body width (![60%](..)). Both nil means "as
    # large as it comes, shrunk to fit".
    attr_accessor :img_w, :img_pct
    # For an :image that names a .mjpg: frames a second (![fps=10](..)) and
    # whether it starts over at the end (![once](..) turns that off). nil
    # leaves both to the renderer's defaults.
    attr_accessor :video_fps, :video_loop
    # Cached Proc for :fmrb_code elements (compiled once by the renderer;
    # eval per render would rerun the mruby compiler on the task C stack).
    attr_accessor :compiled_proc

    def initialize(type, text = nil, level = 0)
      @type = type
      @text = text
      @level = level
      @align = nil
      @size = nil
      @shadow = false
      @number = nil
      @img_w = nil
      @img_pct = nil
      @video_fps = nil
      @video_loop = nil
      @compiled_proc = nil
    end
  end

  class Slide
    attr_reader :title, :elements
    attr_accessor :title_slide
    # Marked with {::goal/}: the finish line of the rabbit's race, so a Q&A
    # or an appendix can sit past it without counting towards the talk's
    # progress (upstream's image-slide-number-last-slide).
    attr_accessor :goal
    # A picture behind this slide from a {::background path/} line: a path,
    # "none" to override the deck's own background with nothing, or nil to
    # take the deck's.
    attr_accessor :background
    # :center from a {::valign center/} line: the body is centred in the
    # space between the title bar and the footer. :top says so explicitly
    # on a deck whose frontmatter centres every page; nil takes the deck's.
    attr_accessor :valign
    # How the heading is dressed, from a {::heading band|underline|plain/}
    # line: nil takes the deck's (band by default -- plain on the title
    # slide, whose picture is meant to carry it).
    attr_accessor :heading

    def initialize(title, elements)
      @title = title
      @elements = elements
      @title_slide = false
      @goal = false
      @background = nil
      @valign = nil
      @heading = nil
    end

    # Count wait markers to determine number of steps
    def wait_count
      count = 0
      @elements.each { |e| count += 1 if e.type == :wait }
      count
    end
  end

  class ParseResult
    attr_reader :slides, :metadata

    def initialize(slides, metadata)
      @slides = slides
      @metadata = metadata
    end

    def theme
      @metadata["theme"]
    end
  end
end
