// ------------------------- OpenPose C++ API Tutorial - Example 13 - Custom Input -------------------------
// Synchronous mode: ideal for production integration. It provides the fastest results with respect to runtime
// performance.
// In this function, the user can implement its own way to create frames (e.g., reading his own folder of images).

// RealSense OpenCV
#include <librealsense2/rs.hpp> // Include RealSense Cross Platform API
#include <opencv2/opencv.hpp>   // Include OpenCV API

// Command-line user intraface
#define OPENPOSE_FLAGS_DISABLE_PRODUCER
#include <openpose/flags.hpp>
// OpenPose dependencies
#include <openpose/headers.hpp>

// Custom OpenPose flags
// Producer
DEFINE_int32(color_width, 1280, "Width of color frame.");
DEFINE_int32(color_height, 720, "Height of color frame.");

// RUIC: img saving and 3d pose extration fully moved to ros side.

// This worker will just read and return all the basic image file formats in a directory
class WRealSenseProducer : public op::WorkerProducer<std::shared_ptr<std::vector<std::shared_ptr<op::Datum>>>>
{
public:
    WRealSenseProducer() :
        // If we want only e.g., "jpg" + "png" images
        // mImageFiles{op::getFilesOnDirectory(directoryPath, std::vector<std::string>{"jpg", "png"})},
        last_frame_number{0}
    {
        rs2::config cfg;
        cfg.enable_stream(RS2_STREAM_COLOR, FLAGS_color_width, FLAGS_color_height, RS2_FORMAT_BGR8, 30);
        pipe.start(cfg);
    }

    void initializationOnThread() {}

    // RUIC: call RealSense OpeCV wrapper, 
    std::shared_ptr<std::vector<std::shared_ptr<op::Datum>>> workProducer()
    {
        try
        {
            rs2::frameset data = pipe.wait_for_frames();
            rs2::frame color_frame = data.get_color_frame();
            if (color_frame.get_frame_number() == last_frame_number)
            {
                // op::log("Last frame read and added to queue. Closing program after it is processed.",
                //         op::Priority::High);
                // This funtion stops this worker, which will eventually stop the whole thread system once all the
                // frames have been processed
                // this->stop();
                return nullptr;
            }
            else
            {
                last_frame_number = color_frame.get_frame_number();
                cv::Mat color_mat = cv::Mat(
                    cv::Size(FLAGS_color_width, FLAGS_color_height),
                    CV_8UC3, (void*)color_frame.get_data(), cv::Mat::AUTO_STEP);

                // Create new datum
                auto datumsPtr = std::make_shared<std::vector<std::shared_ptr<op::Datum>>>();
                datumsPtr->emplace_back();
                auto& datumPtr = datumsPtr->at(0);
                datumPtr = std::make_shared<op::Datum>();

                // Fill datum
                datumPtr->cvInputData = color_mat;

                // If empty frame -> return nullptr
                if (datumPtr->cvInputData.empty())
                {
                    op::log("Color frame is empty.", op::Priority::High);
                    this->stop();
                    datumsPtr = nullptr;
                }

                datumPtr->cvOutputData = datumPtr->cvInputData;
                datumPtr->frameNumber = last_frame_number;

                return datumsPtr;
            }
        }
        catch (const std::exception& e)
        {
            this->stop();
            op::error(e.what(), __LINE__, __FUNCTION__, __FILE__);
            return nullptr;
        }
    }

private:
    unsigned long long last_frame_number;
    rs2::pipeline pipe;
};

void configureWrapper(op::Wrapper& opWrapper)
{
    try
    {
        // Configuring OpenPose

        // logging_level
        op::check(0 <= FLAGS_logging_level && FLAGS_logging_level <= 255, "Wrong logging_level value.",
                  __LINE__, __FUNCTION__, __FILE__);
        op::ConfigureLog::setPriorityThreshold((op::Priority)FLAGS_logging_level);
        op::Profiler::setDefaultX(FLAGS_profile_speed);

        // Applying user defined configuration - GFlags to program variables
        // outputSize
        const auto outputSize = op::flagsToPoint(FLAGS_output_resolution, "-1x-1");
        // netInputSize
        const auto netInputSize = op::flagsToPoint(FLAGS_net_resolution, "-1x368");
        // faceNetInputSize
        const auto faceNetInputSize = op::flagsToPoint(FLAGS_face_net_resolution, "368x368 (multiples of 16)");
        // handNetInputSize
        const auto handNetInputSize = op::flagsToPoint(FLAGS_hand_net_resolution, "368x368 (multiples of 16)");
        // poseMode
        const auto poseMode = op::flagsToPoseMode(FLAGS_body);
        // poseModel
        const auto poseModel = op::flagsToPoseModel(FLAGS_model_pose);
        // JSON saving
        if (!FLAGS_write_keypoint.empty())
            op::log("Flag `write_keypoint` is deprecated and will eventually be removed."
                    " Please, use `write_json` instead.", op::Priority::Max);
        // keypointScaleMode
        const auto keypointScaleMode = op::flagsToScaleMode(FLAGS_keypoint_scale);
        // heatmaps to add
        const auto heatMapTypes = op::flagsToHeatMaps(FLAGS_heatmaps_add_parts, FLAGS_heatmaps_add_bkg,
                                                      FLAGS_heatmaps_add_PAFs);
        const auto heatMapScaleMode = op::flagsToHeatMapScaleMode(FLAGS_heatmaps_scale);
        // >1 camera view?
        // const auto multipleView = (FLAGS_3d || FLAGS_3d_views > 1 || FLAGS_flir_camera);
        const auto multipleView = false;
        // Face and hand detectors
        const auto faceDetector = op::flagsToDetector(FLAGS_face_detector);
        const auto handDetector = op::flagsToDetector(FLAGS_hand_detector);
        // Enabling Google Logging
        const bool enableGoogleLogging = true;

        // Initializing the user custom classes
        // Frames producer (e.g., video, webcam, ...)
        auto wRealSenseProducer = std::make_shared<WRealSenseProducer>();
        // Add custom processing
        const auto workerInputOnNewThread = true;
        opWrapper.setWorker(op::WorkerType::Input, wRealSenseProducer, workerInputOnNewThread);

        // Pose configuration (use WrapperStructPose{} for default and recommended configuration)
        const op::WrapperStructPose wrapperStructPose{
            poseMode,
            netInputSize,
            outputSize,
            keypointScaleMode,
            FLAGS_num_gpu,
            FLAGS_num_gpu_start,
            FLAGS_scale_number,
            (float)FLAGS_scale_gap,
            op::flagsToRenderMode(FLAGS_render_pose,
            multipleView),
            poseModel,
            !FLAGS_disable_blending,
            (float)FLAGS_alpha_pose,
            (float)FLAGS_alpha_heatmap,
            FLAGS_part_to_show,
            FLAGS_model_folder,
            heatMapTypes,
            heatMapScaleMode,
            FLAGS_part_candidates,
            (float)FLAGS_render_threshold,
            FLAGS_number_people_max,
            FLAGS_maximize_positives,
            FLAGS_fps_max,
            FLAGS_prototxt_path,
            FLAGS_caffemodel_path,
            (float)FLAGS_upsampling_ratio,
            enableGoogleLogging
        };
        opWrapper.configure(wrapperStructPose);
        
        // Face configuration (use op::WrapperStructFace{} to disable it)
        const op::WrapperStructFace wrapperStructFace{
            FLAGS_face,
            faceDetector,
            faceNetInputSize,
            op::flagsToRenderMode(FLAGS_face_render,
            multipleView,
            FLAGS_render_pose),
            (float)FLAGS_face_alpha_pose,
            (float)FLAGS_face_alpha_heatmap,
            (float)FLAGS_face_render_threshold
        };
        opWrapper.configure(wrapperStructFace);
        // Hand configuration (use op::WrapperStructHand{} to disable it)
        const op::WrapperStructHand wrapperStructHand{
            FLAGS_hand,
            handDetector,
            handNetInputSize,
            FLAGS_hand_scale_number,
            (float)FLAGS_hand_scale_range,
            op::flagsToRenderMode(FLAGS_hand_render,
            multipleView,
            FLAGS_render_pose),
            (float)FLAGS_hand_alpha_pose,
            (float)FLAGS_hand_alpha_heatmap,
            (float)FLAGS_hand_render_threshold
        };
        opWrapper.configure(wrapperStructHand);

        // Extra functionality configuration (use op::WrapperStructExtra{} to disable it)
        const op::WrapperStructExtra wrapperStructExtra{
            FLAGS_3d,
            FLAGS_3d_min_views,
            FLAGS_identification,
            FLAGS_tracking,
            FLAGS_ik_threads
        };
        opWrapper.configure(wrapperStructExtra);

        // RUIC: Producer config is omitted since setWorker() is used.

        // Output (comment or use default argument to disable any output)
        const op::WrapperStructOutput wrapperStructOutput{
            FLAGS_cli_verbose,
            FLAGS_write_keypoint,
            op::stringToDataFormat(FLAGS_write_keypoint_format),
            FLAGS_write_json,
            FLAGS_write_coco_json,
            FLAGS_write_coco_json_variants,
            FLAGS_write_coco_json_variant,
            FLAGS_write_images,
            FLAGS_write_images_format,
            FLAGS_write_video,
            FLAGS_write_video_fps,
            FLAGS_write_video_with_audio,
            FLAGS_write_heatmaps,
            FLAGS_write_heatmaps_format,
            FLAGS_write_video_3d,
            FLAGS_write_video_adam,
            FLAGS_write_bvh,
            FLAGS_udp_host,
            FLAGS_udp_port
        };
        opWrapper.configure(wrapperStructOutput);

        // GUI (comment or use default argument to disable any visual output)
        const op::WrapperStructGui wrapperStructGui{
            op::flagsToDisplayMode(FLAGS_display, FLAGS_3d),
            !FLAGS_no_gui_verbose,
            FLAGS_fullscreen
        };
        opWrapper.configure(wrapperStructGui);
        // Set to single-thread (for sequential processing and/or debugging and/or reducing latency)
        if (FLAGS_disable_multi_thread)
            opWrapper.disableMultiThreading();
    }
    catch (const std::exception& e)
    {
        op::error(e.what(), __LINE__, __FUNCTION__, __FILE__);
    }
}

int run()
{
    try
    {
        op::log("Starting OpenPose demo...", op::Priority::High);
        const auto opTimer = op::getTimerInit();

        // OpenPose wrapper
        op::log("Configuring OpenPose...", op::Priority::High);
        op::Wrapper opWrapper;
        configureWrapper(opWrapper);

        // Start, run, and stop processing - exec() blocks this thread until OpenPose wrapper has finished
        op::log("Starting thread(s)...", op::Priority::High);
        opWrapper.exec();

        // Measuring total time
        op::printTime(opTimer, "OpenPose demo successfully finished. Total time: ", " seconds.", op::Priority::High);

        // Return
        return 0;
    }
    catch (const std::exception&)
    {
        return -1;
    }
}

int main(int argc, char *argv[])
{
    // Parsing command line flags
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // Running tutorialApiCpp
    return run();
}
