# Family mruby OS - Kernel Framework
# Kernel provides system-level services for the Family mruby OS
class AppData
  def initialize()
    @name
    @path
    @pos_x = 0
    @pos_y = 0
    @window_width = 0
    @window_height = 0
    @visible = true
  end
end

class KernelHandler
  def initialize()
    puts "[KERNEL] initialize"
    @app_list = []
    @window_order = []
    _init()
    puts "[KERNEL] Tick = #{@tick}"
    puts "[KERNEL] Max App Number = #{@max_app_num}"
  end

  def spawn_app_req
    result = _spawn_app_req
    if @app_list < @max_app_num
    AppData.new
    send_response()
  end

  def msg_handler(msg) # called from _spin
    # msg from app
    # - spawn_app_req
    # msg from host
    # - mouse_click
  end

  def tick_process
    #
  end

  def main_loop
    puts "[KERNEL] main_loop started"
    loop do
      tick_process
      _spin(@tick)
    end
  end
end

handler = KernelHandler.new
handler.start
