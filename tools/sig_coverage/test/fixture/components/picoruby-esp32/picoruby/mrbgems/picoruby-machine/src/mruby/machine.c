/* A fixture: an upstream gem, of which only the listed classes count. */
void
mrb_picoruby_machine_gem_init(mrb_state *mrb)
{
  struct RClass *module_Machine = mrb_define_module_id(mrb, MRB_SYM(Machine));
  mrb_define_module_function_id(mrb, module_Machine, MRB_SYM(board_millis), m_millis, MRB_ARGS_NONE());
  struct RClass *other = mrb_define_class(mrb, "Interrupt", mrb->object_class);
  mrb_define_method(mrb, other, "not_counted", m_x, MRB_ARGS_NONE());
}
