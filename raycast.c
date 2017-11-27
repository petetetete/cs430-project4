// Include header file
#include "raycast.h"


double rayObjectIntersect(object_t **outObject, vector3_t origin,
                          vector3_t direction, object_t **scene,
                          int numObjects) {

  // Track closest object
  object_t *closestObject = NULL;
  double closestT = INFINITY;

  // Iteration objects
  object_t *currObject = NULL;
  double currT;

  // Iterate through all objects to find nearest object
  for (int i = 0; i < numObjects; i++) {

    currObject = scene[i]; // Save current object

    // Check for intersection (depending on object type)
    switch (currObject->kind) {
      case OBJECT_KIND_SPHERE:
        currT = sphereIntersect(origin, direction, (sphere_t *) currObject);
        break;
      case OBJECT_KIND_PLANE:
        currT = planeIntersect(origin, direction, (plane_t *) currObject);
        break;
    }

    // If the current t was closer than all before, save the color
    if (currT != NO_INTERSECTION_FOUND && currT < closestT) {
      closestT = currT;
      closestObject = currObject;
    }
  }

  // Check if any intersection was found
  if (closestObject == NULL) {
    outObject = NULL;
    return NO_INTERSECTION_FOUND;
  }
  else {
    if (outObject != NULL)
      *outObject = closestObject;
    return closestT;
  }
}


vector3_t raycast(vector3_t origin, vector3_t direction,
                  object_t **scene, int numObjects,
                  light_t **lights, int numLights, int level) {

  if (level > MAX_RECURSION_LEVEL) {
    return vector3_create(0, 0, 0); // Void color
  }

  // Find the intersection point with the nearest object
  object_t *object;
  double t = rayObjectIntersect(&object, origin, direction, scene, numObjects);

  // If we did not hit any objects, the pixel is in the void
  if (t == NO_INTERSECTION_FOUND) {
    return vector3_create(0, 0, 0); // Void color
  }

  // Calculate color value
  else {
    vector3_t tempVector = vector3_create(0, 0, 0); // Used in calculations

    vector3_t reflectColor = vector3_create(0, 0, 0); // Reflection color
    vector3_t ovDirection = vector3_create(0, 0, 0);
    vector3_t intersect = vector3_create(0, 0, 0);
    vector3_t intersectOffset = vector3_create(0, 0, 0);
    vector3_t normal = vector3_create(0, 0, 0);
    vector3_t reflection = vector3_create(0, 0, 0);
    double illumination;

    /* Calculate values that DO NOT change on a light by light basis */
    illumination = 1.0 - object->reflectivity - object->refractivity;

    vector3_scale(ovDirection, direction, -1);

    // Get intersection point
    vector3_scale(tempVector, direction, t);
    vector3_add(intersect, tempVector, origin);

    // Get object properties
    if (object->kind == OBJECT_KIND_SPHERE) {
      vector3_sub(normal, intersect, ((sphere_t *) object)->position);
      vector3_normalize(normal);
    }
    else if (object->kind == OBJECT_KIND_PLANE) {
      vector3_copy(normal, ((plane_t *) object)->normal);
    }

    // Calculate the object intersect origin by shifting intersect off object
    vector3_scale(tempVector, normal, EPSILON_OFFSET);
    vector3_add(intersectOffset, intersect, tempVector);

    // Calculate reflection vector
    vector3_scale(tempVector, normal, 2*vector3_dot(ovDirection, normal));
    vector3_sub(reflection, tempVector, ovDirection);
    vector3_normalize(reflection);

    // Get reflection color from recursive calls
    reflectColor = raycast(intersectOffset, reflection,
                           scene, numObjects, lights, numLights, level + 1);


    /* Variables that DO change on a light by light basis */
    vector3_t color = vector3_create(0, 0, 0); // No ambient light

    // Declare all variables to be used in light loop
    light_t *light;
    vector3_t olDirection = vector3_create(0, 0, 0);
    double lDistance;
    double shadowObjectT;

    double frad = 1;
    double fang = 1;
    vector3_t diff = vector3_create(0, 0, 0);
    vector3_t spec = vector3_create(0, 0, 0);
    vector3_t lReflection = vector3_create(0, 0, 0);

    // For each light in the world
    for (int i = 0; i < numLights; i++) {

      light = (light_t *) lights[i]; // Current light

      // Get object to light vector and distance
      vector3_sub(olDirection, light->position, intersect);
      lDistance = vector3_mag(olDirection);
      vector3_scale(olDirection, olDirection, 1 / lDistance); // Normalize dir

      // Calculate light reflection vector
      vector3_scale(tempVector, normal, 2*vector3_dot(olDirection, normal));
      vector3_sub(lReflection, tempVector, olDirection);
      vector3_normalize(lReflection);

      // Get the t value of an intersecting object that casts shadows 
      shadowObjectT = rayObjectIntersect(NULL, intersectOffset, olDirection,
                                         scene, numObjects);

      // Only color the object if there isn't an object any closer
      if (shadowObjectT == NO_INTERSECTION_FOUND || shadowObjectT > lDistance) {

        // Calculate the attentuation factors
        frad = radialAttenuation(light, lDistance);
        fang = angularAttenuation(light, olDirection);
        
        // Calculate the diffuse and specular light contributions
        diffuseReflection(diff, object->diffuse_color, light->color, normal,
                          olDirection);
        specularReflection(spec, object->specular_color, light->color,
                           ovDirection, lReflection, 20);

        // Add to color channels
        color[0] += frad * fang * (diff[0] + spec[0]);
        color[1] += frad * fang * (diff[1] + spec[1]);
        color[2] += frad * fang * (diff[2] + spec[2]);
      }
    }

    // Calculate and clamp final color values
    color[0] = clampValue(illumination*color[0] +
                          object->reflectivity*reflectColor[0], 0.0, 1.0);
    color[1] = clampValue(illumination*color[1] +
                          object->reflectivity*reflectColor[1], 0.0, 1.0);
    color[2] = clampValue(illumination*color[2] +
                          object->reflectivity*reflectColor[2], 0.0, 1.0);

    // Clean up allocated memory
    free(tempVector);
    free(reflectColor);
    free(ovDirection);
    free(intersect);
    free(intersectOffset);
    free(normal);
    free(reflection);
    free(olDirection);
    free(diff);
    free(spec);
    free(lReflection);

    return color;
  }
}


// Actually creates and initializes the image, iterates over view plane
int renderImage(ppm_t *ppmImage, camera_t *camera,
                object_t **scene, int numObjects,
                light_t **lights, int numLights) {

  // Iterate over every pixel in the would be image
  double pixHeight = camera->height/ppmImage->height;
  double pixWidth = camera->width/ppmImage->width;

  // Declare the variables only once
  double yCoord;
  double xCoord;
  vector3_t direction = vector3_create(0, 0, 0);
  vector3_t color;

  for (int i = 0; i < ppmImage->height; i++) {
    yCoord = camera->height/2 - pixHeight * (i + 0.5);

    for (int j = 0; j < ppmImage->width; j++) {
      xCoord = -camera->width/2 + pixWidth * (j + 0.5);

      // Create direction vector
      direction[0] = xCoord;
      direction[1] = yCoord;
      direction[2] = -FOCAL_LENGTH;
      vector3_normalize(direction);

      // Get color from raycast
      color = raycast(camera->position, direction,
                      scene, numObjects, lights, numLights, 0);

      // Populate pixel with color data
      ppmImage->pixels[i*ppmImage->width + j].r = (int) (color[0] * 255);
      ppmImage->pixels[i*ppmImage->width + j].g = (int) (color[1] * 255);
      ppmImage->pixels[i*ppmImage->width + j].b = (int) (color[2] * 255);
    }
  }

  return 0; // No errors!
}


int main(int argc, char *argv[]) {

  // Check for the appropriate number of parameters
  if (argc != 5) {
    fprintf(stderr, USAGE_MESSAGE);
    return 1;
  }

  // Save command line parameters
  int viewWidth = atoi(argv[1]);
  int viewHeight = atoi(argv[2]);
  char *inputFName = argv[3];
  char *outputFName = argv[4];

  if (viewWidth <= 0 || viewHeight <= 0) {
    fprintf(stderr, "Error: Invalid width or height, must be > 0\n");
    return 1;
  }

  // Initialize variables to be used in program
  FILE *inputFH;
  FILE *outputFH;
  camera_t *camera = malloc(sizeof(camera_t));
  object_t **scene = malloc(sizeof(object_t) * MAX_SCENE_OBJECTS);
  light_t **lights = malloc(sizeof(light_t) * MAX_SCENE_LIGHTS);
  int *numObjects;

  // Create final ppmImage
  ppm_t *ppmImage = malloc(sizeof(ppm_t));
  ppmImage->width = viewWidth;
  ppmImage->height = viewHeight;
  ppmImage->maxColorValue = 255;
  ppmImage->pixels = malloc(sizeof(pixel_t)*ppmImage->width*ppmImage->height);

  // Handle input file errors
  if (!(inputFH = fopen(inputFName, "r"))) {
    fprintf(stderr, "Error: Input file '%s' could not be found\n", inputFName);
    return 1;
  }

  // Parse input csv into scene object
  numObjects = parseInput(camera, scene, lights, inputFH);

  // Handle errors found in parseInput
  if (numObjects == NULL) {
    fprintf(stderr, "Error: Malformed input CSV\n");
    return 1;
  }

  // Create actual PPM image from scene
  renderImage(ppmImage, camera, scene, numObjects[0], lights, numObjects[1]);

  // Handle open errors on output file
  if (!(outputFH = fopen(outputFName, "w"))) {
    fprintf(stderr, "Error: Unable to open '%s' for writing\n", outputFName);
    return 1;
  }

  // Write final raycasted product to the designated PPM file
  writePPM(ppmImage, outputFH, PPM_OUTPUT_VERSION);

  // Final program clean up
  fclose(inputFH);

  return 0;
}
