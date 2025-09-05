.PHONY : all

#n.b. the '-y' sets autoexec scripts to 'on' so that driver expressions will work
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	BLENDER = /Applications/Blender.app/Contents/MacOS/Blender -y
else ifeq ($(UNAME_S),Linux)
	BLENDER = ../../blender-4.5.2-linux-x64/blender -y
else
	BLENDER = "blender"
endif

EXPORT_MESHES=export-meshes.py
EXPORT_SCENE=export-scene.py

DIST=../dist

all : \
	$(DIST)/tireler.pnct \
	$(DIST)/medal.pnct \
	$(DIST)/meteor.pnct \
	$(DIST)/building.pnct \
	$(DIST)/tree.pnct \
	$(DIST)/spring.pnct \
	$(DIST)/burnin.scene \


$(DIST)/burnin.scene : burnin.blend $(EXPORT_SCENE)
	$(BLENDER) --background --python $(EXPORT_SCENE) -- '$<':Ground '$@'

$(DIST)/tireler.pnct : burnin.blend $(EXPORT_MESHES)
	$(BLENDER) --background --python $(EXPORT_MESHES) -- '$<':Tire '$@'

$(DIST)/medal.pnct : burnin.blend $(EXPORT_MESHES)
	$(BLENDER) --background --python $(EXPORT_MESHES) -- '$<':Medal '$@'

$(DIST)/meteor.pnct : burnin.blend $(EXPORT_MESHES)
	$(BLENDER) --background --python $(EXPORT_MESHES) -- '$<':Meteor '$@'

$(DIST)/building.pnct : burnin.blend $(EXPORT_MESHES)
	$(BLENDER) --background --python $(EXPORT_MESHES) -- '$<':Building '$@'

$(DIST)/tree.pnct : burnin.blend $(EXPORT_MESHES)
	$(BLENDER) --background --python $(EXPORT_MESHES) -- '$<':Tree '$@'

$(DIST)/spring.pnct : burnin.blend $(EXPORT_MESHES)
	$(BLENDER) --background --python $(EXPORT_MESHES) -- '$<':Spring '$@'
