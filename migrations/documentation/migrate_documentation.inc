<?php
/**
 * @file
 * migrate_documentation.inc
 */


/**
 * This Migration class will transfer content of an HTML page into Drupal
 */
abstract class WetKitDocMigration extends Migration {

  /**
   * Default class constructor.
   */
  public function __construct($arguments) {
    // Always call the parent constructor first for basic setup.
    parent::__construct($arguments);

    // With migrate_ui enabled, migration pages will indicate people involved in
    // the particular migration, with their role and contact info. We default
    // the list in shared class; it can be overridden for specific migrations.
    $this->team = array(
      new MigrateTeamMember('William Hearn', 'william.hearn@statcan.gc.ca', t('Web Developer')),
    );
  }
}

/**
 * Migration class for the WET-BOEW docs on the Web Experience Toolkit website.
 */
class WetKitMigrateDocumentation extends WetKitDocMigration {

  /**
   * Implements the migration constructor.
   */
  public function __construct($arguments) {
    parent::__construct($arguments);

    // Human-friendly description of your migration process.
    $this->description = t('Migrate the WET-BOEW documentation from the Web Experience Toolkit');

    // Array of directory paths that will be scanned for files.
    $list_dirs = array(variable_get('WetKitMigrateDocumentation_listdir'));

    // The base dir is the part of the path that will be excluded when making
    // an ID for each file.
    $base_dir = variable_get('WetKitMigrateDocumentation_basedir');

    // Passed on and used to filter for certain types of files. Use a regular
    // expression.
    $file_mask = '/(.*\.htm$|.*\.html$)/i';

    $this->map = new MigrateSQLMap($this->machineName,
        array(
          'sourceid' => array(
            'type' => 'varchar',
            'length' => 255,
            'not null' => TRUE,
          ),
        ),
        MigrateDestinationNode::getKeySchema()
      );

    // The source fields.
    $fields = array(
      'title' => t('Title'),
      'body' => t('Body'),
      'uid' => t('User id'),
      'sourceuri' => t('Source URI'),
      'language' => t('Language'),
      'path' => t('URL Path Alias'),
      'tnid' => t('Translation set node ID'),
      'status' => t('Publication status'),
      'promotion' => t('Promotion to front page flag'),
      'datestamp' => t('The date and time from the Date meta tag'),
      'wetkit_term' => t('A taxonomy field for the migration classification'),
      'columns' => t('Whether the column should be set as either single or multiple'),
    );

    // Create a file source.
    $list_files = new MigrateListFiles($list_dirs, $base_dir, $file_mask);
    $item_file = new MigrateItemFile($base_dir);
    $this->source = new MigrateSourceList($list_files, $item_file, $fields);

    // Set the destination.
    $node_options = MigrateDestinationNode::options(NULL, 'wetkit_html_text');
    $this->destination = new MigrateDestinationNode('wetkit_documentation', $node_options);

    // Map the fields from the source to the new node.
    $this->addFieldMapping('uid', 'uid');
    $this->addFieldMapping('title', 'title');
    $this->addFieldMapping('body', 'body');
    $this->addFieldMapping('body:format', 'format')
      ->defaultValue('wetkit_wysiwyg_text');
    $this->addFieldMapping('field_uri', 'sourceuri');
    $this->addFieldMapping('language', 'language');
    $this->addFieldMapping('path', 'path');
    $this->addFieldMapping('tnid', 'tnid');
    $this->addFieldMapping('status', 'status');
    $this->addFieldMapping('promote', 'promotion');
    $this->addFieldMapping('created', 'datestamp');
    $this->addFieldMapping('changed', 'datestamp');
    $this->addFieldMapping('comment')->defaultValue(FALSE);

    // Indicate which fields are explictly ignored.
    $this->addUnmigratedDestinations(array(
      'sticky',
      'revision',
      'log',
      'revision_uid',
      'is_new',
      'body:summary',
      'body:format',
      'body:language',
      'field_featured_categories:source_type',
      'field_featured_categories:create_term',
      'field_uri:format',
      'field_uri:language',
    ));

    $this->addFieldMapping('field_column', 'columns');
    $this->addFieldMapping('field_featured_categories', 'wetkit_term');
  }

  /**
   * Process the file prior to the actual migration.
   *
   * @param array $row
   *   Row to import for the migration.
   */
  public function prepareRow($row) {

    // Array of DOM Elements to remove calls a Querypath Removal Function.
    $this->elements_to_remove = array(
      'removeDOMElements' => array(
        '0' => '#base-date-mod',
        '1' => '#gcwu-date-mod',
      ),
    );

    // Set to admin for now.
    $row->uid = 1;

    // Create a WetKit HTML Migration Page object. This object takes care of
    // parsing the raw html content.
    if (isset($this->elements_to_remove)) {
      $htmlpage = new WetKitMigrationhtmlpage($row, $this->elements_to_remove);
    }
    else {
      $htmlpage = new WetKitMigrationhtmlpage($row);
    }

    // Set some source fields.
    $row->body = $htmlpage->body;

    // If the body is empty, don't import this row.
    if (strlen(trim($row->body)) == 0) {
      return FALSE;
    }

    $row->title = $htmlpage->title;

    // If the title is empty, don't import this row.
    if (strlen(trim($row->title)) == 0) {
      return FALSE;
    }

    // Set the boolean for whether single or multiple columns.
    $row->columns = $htmlpage->columns;

    // Set the language of the content.
    $row->language = $htmlpage->language;

    // Set the status of the content to published.
    // The node status is 1 for published and 0 for unpublished.
    $row->status = 1;

    // The page promotion to front page flag.
    // 1 for promoted, 0 for not promoted.
    $row->promotion = 0;

    // Set the datestamp the value from the meta tag.
    $row->datestamp = $htmlpage->datestamp;

    // Set up the path - use the file path, but strip away the .htm or .html.
    $row->path = trim($row->sourceid, '\\/');
    $pathstats = pathinfo($row->path);
    $row->dirname = $pathstats['dirname'];
    $row->path = $pathstats['dirname'] . '/' . $pathstats['filename'];
    $row->sourceuri  = substr($row->path, 0, strlen($row->path) - 4);

    // Set up the TNID (the translated node id). To set the TNID, just ensure
    // that every node in the translation set uses the same node ID in the TNID
    // field. To do this, check to see if any other node has the same URI. If it
    // exists then set the TNID of the current row to the Node ID of the
    // matching node.
    $row->tnid = 0;
    $result = db_query('SELECT n.entity_id
      FROM {field_data_field_uri} n WHERE n.field_uri_value = :source_uri', array(':source_uri' => $row->sourceuri));
    foreach ($result as $result_row) {
      $row->tnid = $result_row->entity_id;
      break;
    }

    // If the TNID was set, it is also necessary to update the TNID field in the
    // matching node.
    if ($row->tnid != 0) {
      db_query('UPDATE {node} SET tnid = :tnid WHERE nid = :nid',
          array(':tnid' => $row->tnid, ':nid' => $row->tnid));
    }

    // Process all of the internal links
    if (substr($row->dirname, 0, 6) == 'demos/') {
      $dirname = substr($row->dirname, 6);
    }

    $link_parser = new LinksParser($row->body, variable_get('WetKitMigrateDocumentation_files'), $dirname);

    // Paste the new body text.
    $row->body = $link_parser->process();
  }
}
