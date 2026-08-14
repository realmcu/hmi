/**
 * @file    handlers_register.h
 * @brief   Register all command handlers with the L2 dispatcher.  Called
 *          once from ebadge_task startup path.
 */
#ifndef _EBADGE_HANDLERS_REGISTER_H_
#define _EBADGE_HANDLERS_REGISTER_H_

#ifdef __cplusplus
extern "C" {
#endif

void ebadge_handlers_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _EBADGE_HANDLERS_REGISTER_H_ */
