// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import java.util.Optional;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class DeleteXClusterConfig extends XClusterConfigTaskBase {

  @Inject
  protected DeleteXClusterConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    Universe sourceUniverse = null;
    Universe targetUniverse = null;
    if (xClusterConfig.sourceUniverseUUID != null) {
      sourceUniverse = Universe.getOrBadRequest(xClusterConfig.sourceUniverseUUID);
    }
    if (xClusterConfig.targetUniverseUUID != null) {
      targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);
    }
    try {
      if (sourceUniverse != null) {
        // Lock the source universe.
        lockUniverseForUpdate(sourceUniverse.universeUUID, sourceUniverse.version);
      }
      try {
        if (targetUniverse != null) {
          // Lock the target universe.
          lockUniverseForUpdate(targetUniverse.universeUUID, targetUniverse.version);
        }

        createXClusterConfigSetStatusTask(XClusterConfig.XClusterConfigStatusType.Updating)
            .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.DeleteXClusterReplication);

        // Create all the subtasks to delete the xCluster config and all the bootstrap ids related
        // to them if any.
        createDeleteXClusterConfigSubtasks(xClusterConfig, taskParams().isForced());

        if (targetUniverse != null) {
          createMarkUniverseUpdateSuccessTasks(targetUniverse.universeUUID)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        }

        if (sourceUniverse != null) {
          createMarkUniverseUpdateSuccessTasks(sourceUniverse.universeUUID)
              .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
        }

        getRunnableTask().runSubTasks();
      } finally {
        if (targetUniverse != null) {
          // Unlock the target universe.
          unlockUniverseForUpdate(targetUniverse.universeUUID);
        }
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Optional<XClusterConfig> mightDeletedXClusterConfig = maybeGetXClusterConfig();
      if (mightDeletedXClusterConfig.isPresent()
          && !isInMustDeleteStatus(mightDeletedXClusterConfig.get())) {
        setXClusterConfigStatus(XClusterConfigStatusType.DeletionFailed);
      }
      throw new RuntimeException(e);
    } finally {
      if (sourceUniverse != null) {
        // Unlock the source universe.
        unlockUniverseForUpdate(sourceUniverse.universeUUID);
      }
    }

    log.info("Completed {}", getName());
  }
}
