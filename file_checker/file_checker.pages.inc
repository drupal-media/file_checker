<?php

/**
 * @file file_checker.pages.inc
 * Lists and configuration pages for the file checker.
 */

/**
 * Configuration options form of the file checker.
 * @return
 *   form array
 */
function file_checker_admin_settings($form_state) {
  $form = array();
   
  $form['file_checker_process_on_cron'] = array(
    '#type' => 'radios',
    '#title' => t('Process on cron'),
    '#default_value' => variable_get('file_checker_process_on_cron', 0),
    '#options' => array(0 => t('Disabled'), 1 => t('Enabled')),
    '#description' => t('Do automatic file checking on each cron run. Enable this option only on small sites with a few files.<br/>You can process the file checking manually below or run the drush command "drush file-checker-run"'),
  );

  $form['file_checker_process_batch_size'] = array(
    '#type' => 'textfield',
    '#title' => t('Process batch size'),
    '#default_value' => variable_get('file_checker_process_batch_size', 10000),
    '#description' => t('Process file checking in junks instead of one by one preventing timeouts, memory outages etc.<br/>A higher values usually results in better performance whereas a lower value increases percentage bar accuracy.'), 
  );
  
  $form['file_checker_process_manually'] = array(
    '#type' => 'item',
    '#title' => t('Process manually'),
  );
  
  $total = db_result(db_query(db_rewrite_sql(file_checker_build_count_query()))); // total number of files
  $total_files = t('There are currently @total files.', array('@total' => $total));
  if (!variable_get('file_checker_last_run', 0)) {
    $form['file_checker_process_manually']['#value'] = $total_files . ' ' . t('File verification has never been run.');
  }
  else { 
    $form['file_checker_process_manually']['#value'] = $total_files . ' ' . t('File verification has been last run on @time', array('@time' => format_date(variable_get('file_checker_last_run', 0), 'small')));
  }
  
  $form['file_checker_process_manually_run'] = array(
    '#type' => 'submit',
    '#value' => t('Flag missing files'),
    '#suffix' => '<br/><br/>',
  );
  
  $form['#submit'][] = 'file_checker_admin_settings_submit';
  
  return system_settings_form($form);
}

/**
 * Submit handler for admin settings form.
 * @param $form
 *   form array
 * @param $form_state
 *   form values
 */
function file_checker_admin_settings_submit($form, $form_state) {
  // Run file checking manually
  if ($form_state['clicked_button']['#id'] == 'edit-file-checker-process-manually-run') {
    file_checker_process_verification(FALSE); // batch_process is triggered automatically
  }
}

/**
 * File overview page list all files including some filters and sorting.
 */
function file_checker_overview() {
  
  $output = drupal_get_form('file_checker_form_overview'); // filter form

  // define table headers here
  $header = array(
    array('data' => t('File ID'), 'field' => 'f.fid'),
    array('data' => t('File name'), 'field' => 'f.filename'),
    array('data' => t('File path'), 'field' => 'f.filepath'),
    array('data' => t('File Mime'), 'field' => 'f.filemime'),
    array('data' => t('File size'), 'field' => 'f.filesize'),
    array('data' => t('Timestamp'), 'field' => 'f.timestamp', 'sort' => 'desc'),
    array('data' => t('User'), 'field' => 'f.uid'),
    array('data' => t('Status'), 'field' => 'f.status'),
  );
  
  $tablesort = tablesort_sql($header); // generate sort sql query snippet

  list($sql, $args) = _file_checker_build_overview_query(); // build base query plus arguments
  
  $result = pager_query($sql . $tablesort, 50, 0, NULL, $args);

  while ($file = db_fetch_object($result)) {
    $rows[] = array('data' =>
      array(
        $file->fid,
        $file->filename,
        $file->filepath,
        $file->filemime,
        $file->filesize,
        format_date($file->timestamp, 'small'),
        l(user_load($file->uid)->name, 'user/' . $file->uid),
        $file->status,
      )
    );   
  }

  if (!$rows) {
    $rows[] = array(array('data' => t('No files available.'), 'colspan' => 8));
  }

  $output .= theme('table', $header, $rows);
  $output .= theme('pager', NULL, 50, 0);

  return $output;
}

/**
 * Filter form for overview page.
 * TODO: Use advanced filtering as in user_filter_form() and theme_user_filters().
 * 
 * @param $form_state
 *   array of form values
 * @return 
 *   form array
 */
function file_checker_form_overview($form_state) {

  $form['filters'] = array(
    '#type' => 'fieldset',
    '#title' => t('Show only items where'),
  );

  $form['filters']['filter_filename'] = array(
    '#type' => 'textfield',
    '#title' => t('File name'),
    '#default_value' => $_SESSION['file_checker_overview_filter_filename']
  );

  $names[''] = t('All mime types');
  foreach (_file_checker_get_file_mime_types() as $type) {
    $names[$type] = t('!type', array('!type' => t($type)));
  }
  $form['filters']['filter_filemime'] = array(
    '#type' => 'select',
    '#title' => t('File Mime type'),
    '#options' => $names,
    '#default_value' => $_SESSION['file_checker_overview_filter_filemime'] ? $_SESSION['file_checker_overview_filter_filemime'] : '',
  );
  $form['filters']['filter_uid'] = array(
    '#type' => 'textfield',
    '#title' => t('User Id'),
    '#default_value' => $_SESSION['file_checker_overview_filter_uid']
  );
  $form['filters']['filter_status'] = array(
    '#type' => 'select',
    '#title' => t('File status'),
    '#options' => array('' => t('Any'), 0 => t('Temporary'), 1 => t('Permanent'), 2 => t('Missing')),
    '#default_value' => $_SESSION['file_checker_overview_filter_status'] ? $_SESSION['file_checker_overview_filter_status'] : 2,
  );

  $form['submit'] = array('#type' => 'submit', '#value' => t('Filter'));
  $form['buttons']['reset'] = array('#type' => 'submit', '#value' => t('Reset'));
  $form['#redirect'] = FALSE;

  return $form;
}

/**
 * Submit handler for filter form.
 * @param $form
 *   form array
 * @param $form_state
 *   form values
 */
function file_checker_form_overview_submit($form, &$form_state) {
  switch ($form_state['values']['op']) {
    case t('Reset'): // reset form
      foreach ($form_state['values'] as $form_value_id => $form_value) {
        if (strpos($form_value_id, 'filter_') === 0) {
          unset($_SESSION["file_checker_overview_$form_value_id"]);
        }
      }
      break;
    case t('Filter'): // filter form
      foreach ($form_state['values'] as $form_value_id => $form_value) {
        if (strpos($form_value_id, 'filter_') === 0) {
          if ($form_value) {
            $_SESSION["file_checker_overview_$form_value_id"] = $form_value;
          }
          else {
            unset($_SESSION["file_checker_overview_$form_value_id"]);
          }
        }
      }
      break;
  }

}

/**
 * Get a list of file mime types.
 * 
 * @return
 *   array of file mime types
 */
function _file_checker_get_file_mime_types() {
  $types = array();
  $result = db_query('SELECT DISTINCT(filemime) FROM {files} ORDER BY filemime');
  while ($object = db_fetch_object($result)) {
    $types[] = $object->filemime;
  }
  return $types;
}

/**
 * Build the query for the overview pages considering filters.
 * 
 * @return
 *   array containing SQL statement string and query arguments array
 */
function _file_checker_build_overview_query() {
  $cols = array_keys(file_checker_get_db_table_file_cols());
  $wheres = array();
  $args = array();
  // filters are stored in $_SESSION, build the where SQL snippets from it
  foreach ($cols as $col) {
    $filter = 'file_checker_overview_filter' . "_$col";
    if (isset($_SESSION[$filter])) {            
      $wheres[] = $col;
      $args[] = $_SESSION[$filter];
    }
  }
  $sql = file_checker_build_select_query($wheres);
  return array($sql, $args);
}