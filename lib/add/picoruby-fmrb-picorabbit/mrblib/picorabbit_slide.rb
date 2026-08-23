# PicoRabbit slide data structures
# Compatible with Harucom OS picorabbit format

module PicoRabbit
  class Element
    attr_reader :type, :text, :level
    attr_accessor :align
    # The number a :numbered item was written with, so the list reads the way
    # its author numbered it (1. 1. 1. included) rather than by its position.
    attr_accessor :number
    # How wide an :image was asked to be: a width in pixels (![w=200](..))
    # or a percentage of the body width (![60%](..)). Both nil means "as
    # large as it comes, shrunk to fit".
    attr_accessor :img_w, :img_pct
    # Cached Proc for :fmrb_code elements (compiled once by the renderer;
    # eval per render would rerun the mruby compiler on the task C stack).
    attr_accessor :compiled_proc

    def initialize(type, text = nil, level = 0)
      @type = type
      @text = text
      @level = level
      @align = nil
      @number = nil
      @img_w = nil
      @img_pct = nil
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

    def initialize(title, elements)
      @title = title
      @elements = elements
      @title_slide = false
      @goal = false
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
