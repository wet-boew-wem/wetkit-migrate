<?php
/**
 * @file
 * migrate_default_content.inc
 */

/**
 * Abstract Migration class for Default Content for the Web Experience Toolkit.
 */
abstract class WetKitDCMigration extends XMLMigration {

  /**
   * ProcessImport for WetKit Migration.
   *
   * @param array $options
   *   Process the import array.
   */
  public function processImport(array $options = array()) {
    parent::processImport($options);
    // Do not force menu rebuilding. Otherwise pathauto will try to rebuild
    // in each node_insert invocation.
    variable_set('menu_rebuild_needed', FALSE);
  }
}

/**
 * Migration class for the Default Content for the Web Experience Toolkit
 */
class WetKitMigrateDefaultContent extends WetKitDCMigration {

  /**
   * Implements the migration constructor.
   */
  public function __construct($arguments) {
    parent::__construct($arguments);

    $this->description = t('Migrate the WET-BOEW default content into the Web Experience Toolkit');

    // Create a map object for tracking the relationships between source rows.
    $this->map = new MigrateSQLMap($this->machineName,
      array(
        'sourceid' => array(
          'type' => 'varchar',
          'length' => 255,
          'not null' => TRUE,
        ),
      ),
      MigrateDestinationMenu::getKeySchema()
    );

    // Source fields available in the XML file.
    $fields = array(
      'id' => t('Source id'),
      'title' => t('Title'),
      'body' => t('Description'),
    );

    $xml_folder = DRUPAL_ROOT . '/' . drupal_get_path('module', 'wetkit_migrate') . '/migrations/defaultcontent';
    $items_url = $xml_folder . '/migrate_defaultcontent.xml';
    $item_xpath = '/default_content/*/content';
    $item_id_xpath = 'source_id';

    $items_class = new MigrateItemsXML($items_url, $item_xpath, $item_id_xpath);
    $this->source = new MigrateSourceMultiItems($items_class, $fields);

    // Defaults for Node Destination Migration.
    $language = 'en';
    $text_format = 'wetkit_wysiwyg_text';
    $node_options = MigrateDestinationNode::options($language, $text_format);
    $this->destination = new MigrateDestinationNode('wetkit_page', $node_options);

    // Main Fields.
    $this->addFieldMapping('title', 'title')
        ->xpath('title');
    $this->addFieldMapping('title_field', 'title_field')
      ->xpath('title')
      ->description(t('Title Field for Content Link'));
    $this->addFieldMapping('body', 'body')
      ->xpath('body');
    $this->addFieldMapping('body:format', 'format')
    	->defaultValue('wetkit_wysiwyg_text');
    $this->addFieldMapping('uid')
        ->defaultValue(1);
    $this->addFieldMapping('status')
        ->defaultValue(1);
    $this->addFieldMapping('language', 'language');

    // Extra Fields.
    $this->addFieldMapping('field_featured_image', 'images')
        ->xpath('image');
    $this->addFieldMapping('field_featured_image:alt', 'image_alt')
        ->xpath('image_alt');
    $this->addFieldMapping('field_featured_image:file_replace')
      ->defaultValue(FILE_EXISTS_REPLACE);
    $this->addFieldMapping('field_featured_image:source_dir')
      ->defaultValue(drupal_get_path('module', 'wetkit_migrate') . '/migrations/defaultcontent/images');
    // PathAuto mapping: generate automatic alias.
    if (module_exists('pathauto')) {
      $this->addFieldMapping('pathauto','pathauto')
        ->defaultValue('1');
    }

    // Unmapped destination fields.
    $this->addUnmigratedDestinations(array(
      'is_new',
      'promote',
      'revision',
      'sticky',
      'created',
      'changed',
      'revision_uid',
    ));
  }

  /**
   * Prepare and modify the source rows.
   *
   * @param array $row
   *   Row to import for the migration.
   */
  public function prepareRow($row) {
    // Set the Language.
    $lang = (preg_match("/eng/i", $row->sourceid)) ? 'en' : 'fr';
    $row->language = $lang;
    $xmlobject = $row->xml;
  }

  /**
   * Prepare some of the rows which require extra logic before can be mapped.
   */
  function prepare($entity, stdClass $row) {
    // Default Language.
    $entity->language = 'en';

    // Language of Migrated content.
    $language = $row->language;

    // Title Module support.
    if (module_exists('title')) {
      $entity->title_field[$language][0]['value'] = $row->title;
      $entity->title_field[$language][0]['format'] = 'plain_text';
      $entity->title_field[$language][0]['safe_value'] = $row->title;
    }

    // Set some source fields.
    $entity->body[$language][0]['value'] = $row->body;
    $entity->body[$language][0]['format'] = 'wetkit_wysiwyg_text';
    $entity->body[$language][0]['safe_value'] = $row->body;

    // If the title is empty, don't import this row.
    if (strlen(trim($entity->title_field[$language][0]['value'])) == 0) {
      throw new MigrateException(t("No title detected for: @sourceid", array('@sourceid' => $row->sourceid)), Migration::MESSAGE_NOTICE, MigrateMap::STATUS_IGNORED);
    }

    // If the body is empty, don't import this row.
    if (strlen(trim($entity->body[$language][0]['value'])) == 0) {
      throw new MigrateException(t("No body detected for: @sourceid", array('@sourceid' => $row->sourceid)), Migration::MESSAGE_NOTICE, MigrateMap::STATUS_IGNORED);
    }

    // Map the French Content.
    if ($language == 'fr') {
      // Only one authoritative title, rest is title_field.
      unset($entity->title);

      // Join Handling.
      $sourceid = preg_replace('#_fra_#', '_eng_', $row->sourceid);
      $result = db_query('SELECT n.destid1 FROM {migrate_map_wetkitmigratedefaultcontent} n WHERE n.sourceid1 = :sourceid', array(':sourceid' => $sourceid));
      foreach ($result as $result_row) {
        $entity->nid = $result_row->destid1;
        break;
      }

      // Entity Translation Handler.
      $entity->translations = (object) array(
      'original' => 'en',
        'data' => array(
          'en' => array(
            'entity_type' => 'node',
            'entity_id' => $entity->nid,
            'language' => 'en',
            'source' => '',
            'uid' => '0',
            'status' => '1',
            'translate' => '0',
          ),
          'fr' => array(
            'entity_type' => 'node',
            'entity_id' => $entity->nid,
            'language' => 'fr',
            'source' => 'en',
            'uid' => '1',
            'status' => '1',
            'translate' => '0',
          ),
        )
      );
    }
    return $entity;
  }

 /**
  * By default populate the data source row with fields parsed from the HTML.
  *
  * @param array $row
  *   The prepared row that will be modified.
  */
  public function complete($entity, stdClass $row) {

  }

}
