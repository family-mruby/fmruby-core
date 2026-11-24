class TemplateApp < FmrbApp
  def on_create
    Bar.new
    s = "x"
    s.freeze
  end
end
