# PicoRabbit slide data structures
# Compatible with Harucom OS picorabbit format

module PicoRabbit
  class Element
    attr_reader :type, :text, :level
    attr_accessor :align

    def initialize(type, text = nil, level = 0)
      @type = type
      @text = text
      @level = level
      @align = nil
    end
  end

  class Slide
    attr_reader :title, :elements
    attr_accessor :title_slide

    def initialize(title, elements)
      @title = title
      @elements = elements
      @title_slide = false
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
