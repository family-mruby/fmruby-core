# System GUI Application
# This is the default system GUI that provides basic UI elements

# class SystemGuiApp < FmrbApp
#   def initialize
#     super("SystemGUI")
#     @counter = 0
#   end

#   def on_create
#     puts "[SystemGUI] App created"
#   end

#   def on_update(delta_time_ms)
#     @counter += 1
#     if @counter % 60 == 0  # Every 1 second at 60Hz
#       puts "[SystemGUI] Running... (#{@counter / 60}s)"
#     end
#   end

#   def on_pause
#     puts "[SystemGUI] Paused"
#   end

#   def on_resume
#     puts "[SystemGUI] Resumed"
#   end

#   def on_destroy
#     puts "[SystemGUI] Destroyed"
#   end

#   def on_key_down(key_code)
#     puts "[SystemGUI] Key down: #{key_code}"
#   end
# end

# # Create and start the system GUI app instance
# app = SystemGuiApp.new
# app.start


puts "[SystemGUI] hello"
while true
    puts "gui app loop running"
    Machine.delay_ms(3000)
end