#if !defined(_AMP_CONTROLLER_H_)
#define _AMP_CONTROLLER_H_

#include "esp_err.h"
#include "esp_event.h"
#include <sys/queue.h>

#include "amp/element.h"

/**
 * @brief representing a single controller handle
 */
typedef struct amp_controller *amp_controller_handle_t;

/**
 * @brief Start all appended element tasks
 *
 * @param controller  Controller handle
 * @return
 *     - ESP_OK              All element tasks started successfully
 *     - ESP_FAIL            Failed to start one or more element tasks
 */
esp_err_t amp_controller_run(amp_controller_handle_t controller);

/**
 * @brief Allocate and initialize a controller
 *
 * @param[out] controller  Pointer to the newly created controller handle
 * @return
 *     - ESP_OK              Controller created successfully
 *     - ESP_ERR_NO_MEM      Insufficient memory
 *     - ESP_FAIL            Failed to initialize internal resources
 */
esp_err_t amp_controller_init(amp_controller_handle_t *controller);

/**
 * @brief Deinitialize and free a controller
 *
 * @param controller  Controller handle created by amp_controller_init
 */
void amp_controller_deinit(amp_controller_handle_t controller);

/**
 * @brief Append a reader element to the controller pipeline
 *
 * @param controller  Controller handle
 * @param el          Reader element handle
 * @param intf        Reader element interface
 * @param cfg         Task and ringbuffer configuration for the element
 * @return
 *     - ESP_OK                  Element appended successfully
 *     - ESP_ERR_INVALID_STATE   Controller pipeline state is invalid
 *     - ESP_ERR_NO_MEM          Insufficient memory
 *     - ESP_FAIL                Failed to append the element
 */
esp_err_t amp_controller_append_reader(amp_controller_handle_t controller, amp_element_handle_t el,
                                       const amp_element_task_config_t *cfg);

/**
 * @brief Append a writer element to the controller pipeline
 *
 * @param controller  Controller handle
 * @param el          Writer element handle
 * @param intf        Writer element interface
 * @param cfg         Task and ringbuffer configuration for the element
 * @return
 *     - ESP_OK                  Element appended successfully
 *     - ESP_ERR_INVALID_STATE   Controller pipeline state is invalid
 *     - ESP_ERR_NO_MEM          Insufficient memory
 *     - ESP_FAIL                Failed to append the element
 */
esp_err_t amp_controller_append_writer(amp_controller_handle_t controller, amp_element_handle_t el,
                                       const amp_element_task_config_t *cfg);

/**
 * @brief Append a processor element to the controller pipeline
 *
 * @param controller  Controller handle
 * @param el          Processor element handle
 * @param intf        Processor element interface
 * @param cfg         Task and ringbuffer configuration for the element
 * @return
 *     - ESP_OK                  Element appended successfully
 *     - ESP_ERR_INVALID_STATE   Controller pipeline state is invalid
 *     - ESP_ERR_NO_MEM          Insufficient memory
 *     - ESP_FAIL                Failed to append the element
 */
esp_err_t amp_controller_append_processor(amp_controller_handle_t controller, amp_element_handle_t el,
                                          const amp_element_task_config_t *cfg);

/**
 * @brief Switch the controller to playing state
 *
 * @param controller  Controller handle
 * @return
 *     - ESP_OK                  Play action posted successfully
 *     - ESP_ERR_INVALID_STATE   Controller state does not allow play
 *     - ESP_FAIL                Failed to post the action event
 */
esp_err_t amp_controller_action_play(amp_controller_handle_t controller);

/**
 * @brief Switch the controller to pause state
 *
 * @param controller  Controller handle
 * @return
 *     - ESP_OK                  Pause action posted successfully
 *     - ESP_ERR_INVALID_STATE   Controller state does not allow pause
 *     - ESP_FAIL                Failed to post the action event
 */
esp_err_t amp_controller_action_pause(amp_controller_handle_t controller);

/**
 * @brief Reset the controller to ready state
 *
 * @param controller  Controller handle
 * @return
 *     - ESP_OK                  Reset action posted successfully
 *     - ESP_ERR_INVALID_STATE   Controller state does not allow reset
 *     - ESP_FAIL                Failed to post the action event
 */
esp_err_t amp_controller_action_reset(amp_controller_handle_t controller);

/**
 * @brief Toggle between play and pause state
 *
 * @param controller  Controller handle
 * @param[out] to_play  When not NULL, receives true if the new action is play, false if pause
 * @return
 *     - ESP_OK                  Toggle action completed successfully
 *     - ESP_ERR_INVALID_STATE   Controller state does not allow toggle
 *     - ESP_FAIL                Failed to post the action event
 */
esp_err_t amp_controller_action_toggle_play(amp_controller_handle_t controller, bool *to_play);

#endif // _AMP_CONTROLLER_H_
