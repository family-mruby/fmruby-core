class Foo
  attr_reader :count

  def initialize
    @count = 0
  end

  def self.make = new

  def bar = 1

  def qux = 2

  # @api private
  def wired = 3

  def _scratch = 4

  private def secret = 5

  def method_missing(name, *args) = super

  class << self
    def built? = true

    private

    def hush = nil
  end

  private

  def hidden = 6

  def self.still_public = 7
end

module Tool
  module_function

  def go = 1

  def stop = 2
end
