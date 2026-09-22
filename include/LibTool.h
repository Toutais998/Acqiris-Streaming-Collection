////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) Acqiris SA 2019-2023
//--------------------------------------------------------------------------------------------------
// LibTool: header only library for AqMD3 IVI-C examples.
//
////////////////////////////////////////////////////////////////////////////////////////////////////
#ifndef LIBTOOL_H
#define LIBTOOL_H

#include <sstream>
#include <fstream>
#include <vector>
#include <numeric>
#include <limits>
#include <iomanip>
#include <stdexcept>
#include <list>
#include <algorithm>

namespace LibTool
{
    //! Convert a value to a string.
    /*! Uses the stream insertion operator as defined for the given type to do the conversion. */
    template <typename T>
    inline std::string ToString( T const & value )
    {
        std::ostringstream strm;
        strm << value;
        return strm.str();
    }

    //! Divide `value` by `divider`, always rounding up.
    /*! `value` and `divider` must be integers, and `divider` must be positive.
        \return the smallest integer which is not smaller than `value` divided by `divider`. */
    template <class T>
    inline T CeilDiv(T value, T divider)
    {
        static_assert(std::numeric_limits<T>::is_integer, "Requires integer type");
        if (divider <= 0)
            throw std::invalid_argument("Divider must be positive; got " + ToString(divider));

        if (value > 0 && value > ((std::numeric_limits<T>::max)() - divider + 1))
            throw std::overflow_error("Integer overflow in CeilDiv");

        return (value + ( value > 0 ? divider - 1 : 0)) / divider;
    }

    //! Align a value to the next higher integer multiple of `grain`.
    template <class T>
    inline T AlignUp(T value, T grain)
    {
        static_assert(std::numeric_limits<T>::is_integer, "Requires integer type");

        if (value > (std::numeric_limits<T>::max)() - (grain-1))
            throw std::logic_error("Aligned up value exceeding the numeric limit.");

        return CeilDiv(value, grain) * grain;
    }

    //! Expand the sign of #value (which is nbrBits-bit) and return equivalent value in 32-bit integer representation.
    inline int32_t ExpandSign(int32_t value, int nbrBits)
    {
        if (nbrBits <= 0 || 32 <= nbrBits)
            throw std::invalid_argument("Invalid number of bits for sign expansion: " + ToString(nbrBits));

        if (value >= (int32_t(1) << (nbrBits - 1)))
            return value - (1 << nbrBits);
        return value;
    }

    //! Expand the sign of #value (which is #nbrBits-bit) and return equivalent value in 64-bit integer representation.
    inline int64_t ExpandSign(int64_t value, int nbrBits)
    {
        if (nbrBits <= 0 || 64 <= nbrBits)
            throw std::invalid_argument("Invalid number of bits for sign expansion: " + ToString(nbrBits));

        if (value >= (int64_t(1) << (nbrBits - 1)))
            return value - (int64_t(1) << nbrBits);
        return value;
    }

    //! Convert fixed-point representation of a signed value into a (double precision) float-point representation.
    /*! \param[in] value: the value to scale. It is the integer representation of fixed-point.
        \param[in] nbrIntegerBits: the number of integer bits of the fixed-point representation.
        \param[in] nbrFractionBits: the number of fraction bits of the fixed-point representation.
        \return a float-point representation of the given #value.*/
    inline double ScaleSigned(int32_t value, int nbrIntegerBits, int nbrFractionBits)
    {
        double const scaleFactor = double(1.0) / (1 << nbrFractionBits);
        return double(LibTool::ExpandSign(value, nbrIntegerBits + nbrFractionBits)) * scaleFactor;
    }

    //! Represents a subsegment of a read-only array.
    /*! The class receives a const reference to a read-only instance of 'std::vector'. The class does not own this given instance, and the
        later must not be resized and/or destroyed until the destruction of all associated 'ArraySegment' instances.*/
    template <typename T> class ArraySegment
    {
    private:
        using value_type = T;
        using pointer = T*;
        using container_type = std::vector<T>;

    public:
        //! Build a segment of 'count' elements starting at 'offset' from 'data' container.
        /*! 'data' must not be resized and/or destroyed during the life-cycle of all ArraySegment objects referencing it.*/
        explicit ArraySegment(container_type const& data, size_t offset, size_t count);

        //! Return the size of the segment.
        size_t Size() const { return m_size; }

        //! Return a const reference to the item associated with the given index in the segment.
        value_type const& operator[](size_t index) const
        {
            return m_data[m_offset + index];
        }

        //! Return a pointer of the first element in the segment.
        pointer GetData() const { return pointer(&m_data[m_offset]); }

        //! Skip the first 'nbrElements' elements from the array segment. Size is reduced accordingly, elements are not destroyed.
        void PopFront(size_t nbrElements);

    private:
        container_type const& m_data;   //!< Const reference to the underlaying data container
        size_t m_offset;                //!< offset in 'm_data' of the very first item of the segment.
        size_t m_size;                  //!< size of the segment
    };

    //! Represent tag values of marker packets.
    enum class MarkerTag : uint8_t
    {
        None            = 0x00,
        TriggerNormal   = 0x01, // 512-bit: Trigger marker standard Normal acquisition mode
        TriggerAverager = 0x02, // 512-bit: Trigger marker standard Averager acquisition mode
        GateStartCst    = 0x04, //  64-bit: ZeroSuppress Gate start marker in CST mode.
        GateStopCst     = 0x05, //  64-bit: ZeroSuppress Gate stop marker in CST mode.
        DummyGate       = 0x08, //  64-bit: ZeroSuppress dummy gate marker.
        RecordStop      = 0x0a, //  64-bit: ZeroSuppress record stop marker.
    };

    //! Represent a marker of a trigger
    struct TriggerMarker
    {
        static uint32_t const RecordIndexMask = 0x00ffffff;

        MarkerTag tag = MarkerTag::None; //!< marker tag.
        double triggerTimeSamples=0.0;   /*!< represent the time difference (in sample interval) between trigger and next sampling time. Values are in [0,1[.
                                                This field does not include trigger delay.*/
        uint64_t absoluteSampleIndex=0;  //!< the absolute index (since module init/reset) of the very first sample of acquisition.
        uint32_t recordIndex=0;          //!< index of the record.

                                           //! Return the absolute time of the very first sample of record.
        double GetInitialXTime(double timestampPeriod) const
        { return double(absoluteSampleIndex)*timestampPeriod; }

        //! Return the time difference between the very first sample of record and trigger event.
        double GetInitialXOffset(double samplePeriod, double triggerDelaySeconds=0.0) const
        { return triggerTimeSamples*samplePeriod + triggerDelaySeconds; }

        double GetInitialSampleOffset() const
        { return triggerTimeSamples;}
    };


    //! Special namespace gathering helper functions associated with marker stream decoding.
    namespace StandardStreaming
    {
        using MarkerStream = ArraySegment<int32_t>;

        //! Expect a trigger marker from the input marker stream, decode it and return it as a result.
        TriggerMarker DecodeTriggerMarker(MarkerStream&);

        //! Tell whether the given tag corresponds to a trigger marker.
        inline bool IsTriggerMarkerTag(MarkerTag tag)
        { return tag == MarkerTag::TriggerNormal || tag == MarkerTag::TriggerAverager; }

        //! Extract the marker tag from the given header.
        inline MarkerTag ExtractTag(uint32_t element)
        { return MarkerTag(element & 0xff); }

        static constexpr size_t NbrTriggerMarkerElements = 16; // 512-bit (16 elements of 32-bit)
    }

    //! ZeroSuppress related utils.
    namespace ZeroSuppress
    {
        //! Represent processing and storage parameters
        struct ProcessingParameters
        {
            int32_t storageBlockSamples;     //!< number of samples in a memory block
            int32_t processingBlockSamples;  //!< number of samples in a processing block
            double timestampPeriod;          //!< timestamp period in seconds
            int32_t preGateSamples;          //!< the number of pre-gate samples
            int32_t postGateSamples;         //!< the number of post-gate samples

            explicit ProcessingParameters(int32_t storageSamples, int32_t processingSamples, double tsPeriod, int32_t preGate, int32_t postGate)
                : storageBlockSamples(storageSamples)
                , processingBlockSamples(processingSamples)
                , timestampPeriod(tsPeriod)
                , preGateSamples(preGate)
                , postGateSamples(postGate)
            {}
        };

        //! Represent a marker of a gate start
        class GateStartMarker
        {
        private:
            int64_t m_blockIndex;           //!< gate start position (in processing blocks)
            int32_t m_startSampleIndex;     //!< the index of the first sample of the gate (the one obove the threshold).

        public:
            //! Construct a start marker object from a raw 64-bit marker.
            /* The 64-bit raw marker is represented by two 32-bit elements (element0 & element1).
               \param element0: the least significant part of the raw marker.
               \param element1: the most significant part of the raw marker.*/
            explicit GateStartMarker(int32_t element0, int32_t element1);

            //! Return the number of the leading suppressed samples in the first block of the gate.
            int64_t GetSuppressedSampleCount(ProcessingParameters const&) const { return m_startSampleIndex; }

            //! Return the index (in the record) of the sample where the gate start condition is met.
            /*! It is the very first sample which goes above the configured zero-suppress threshold.*/
            int64_t GetStartSampleIndex(ProcessingParameters const& params) const
            {
                return int64_t(m_blockIndex - 1) * params.processingBlockSamples + GetSuppressedSampleCount(params);
            }

            //! Return the block index of start gate position.
            int64_t GetBlockIndex() const { return m_blockIndex; }
        };

        //! Represent a stop marker, either gate-stop marker or record-stop marker.
        class StopMarker
        {
        private:
            int64_t m_blockIndex;       //!< gate stop position (in processing blocks)
            int32_t m_gateEndIndex;     //!< the index of one-past last sample of the gate. if IsRecordStop() returns `true` it indicates the index of the last sample of the gate.
            MarkerTag m_tag;            //!< marker tag type.

        public:
            //! Constructs stop marker object from a raw 64-bit marker.
            /* The 64-bit raw marker is represented by two 32-bit elements (element0 & element1).
               \param element0: the least significant part of the raw marker.
               \param element1: the most significant part of the raw marker.*/
            explicit StopMarker(int32_t element0, int32_t element1);

            explicit StopMarker();

            //! Return the number of the trailing suppressed samples in the last block of the gate.
            int64_t GetSuppressedSampleCount(ProcessingParameters const& params) const
            {
                return (params.processingBlockSamples - m_gateEndIndex) - (IsRecordStop() ? 1 : 0);
            }

            //! Return the index (in the record) of the sample where the gate stop condition is met.
            /*! It is the very first sample which goes below the configured ZeroSuppress threshold-Hysteresis.*/
            int64_t GetStopSampleIndex(ProcessingParameters const& params) const
            {
                return int64_t(m_blockIndex - 1) * params.processingBlockSamples - GetSuppressedSampleCount(params);
            }

            //! Returns true when the gate is ended by a record-stop.
            /*! This means that part of the gate is not included in the record (because located outside the record acquisition window)*/
            bool IsRecordStop() const { return m_tag == MarkerTag::RecordStop; }

            //! Return the block index of stop position.
            int64_t GetBlockIndex() const { return m_blockIndex; }
        };

        using GateStopMarker = StopMarker;
        using RecordStopMarker = StopMarker;

        //! Represent a marker of a gate (composed of gate start and gate stop markers)
        /*! A gate descriptor describes data samples stored in memory in the following way:

                                                         samples as they are stored in memory
                               +---------+-----------------+--------------------------------+----------------+---------+
                               | padding |  pre gate       |          gate                  |      post gate | padding |
                               +---------+-----------------+--------------------------------+----------------+---------+
                                                           ^                                 ^
                                                           |                                 |
                                                           +-------+       +-----------------+
                                                                   |       |
                                                             +-----+---+---+----+
                                                             |  start  |  stop  |
                                                             +---------+--------+
                                                                gate-descriptor

            Where:
            - The leading padding corresponds to the samples before the start-gate sample (in the same processing block).
              When pre-gate is different than zero, these samples correspond to samples before pre-gate.
            - The "start" index (returned by #GateStartMarker::GetStartSampleIndex) indicates the first sample of the gate
              (the sample which exceeds the configured threshold).
            - The "stop" index (returned by #GateStopMarker::GetStopSampleIndex) indicates the one-past last sample of the
              gate. It is the first sample falling below threshold-hysteresis.
            - The trailing padding corresponds to: 1) samples located after the gate-stop (or post-gate) in the same
              processing block, and/or 2) extra padding added to fit DDR alignment constraint (512-bit).*/
        class GateMarker
        {
        public:
            explicit GateMarker(GateStartMarker const& startMarker, GateStopMarker const& stopMarker);

            //! Return the number of samples associated with the gate stored in memory (including padding).
            int64_t GetStoredSampleCount(ProcessingParameters const& params, RecordStopMarker const& recordStop) const;

            //! Extract gate position from a raw 64-bit marker (gate start or gate stop).
            /* The 64-bit raw marker is represented by two 32-bit elements, where element0 is the least significant part.*/
            static int64_t ExtractPosition(int32_t element0, int32_t element1);

            //! Return a const reference to start marker
            GateStartMarker const& GetStartMarker() const { return m_startMarker; }

            //! Return a const reference to stop marker
            GateStopMarker  const& GetStopMarker() const { return m_stopMarker; }
        private:
            GateStartMarker m_startMarker;  //!< gate start marker
            GateStopMarker m_stopMarker;    //!< gate stop marker
        };

        //! Represent a record descriptor with trigger marker and a list of gate markers.
        class RecordDescriptor
        {
        public:
            using GateList = std::vector<GateMarker>;

            RecordDescriptor(RecordDescriptor const&) = default;

            explicit RecordDescriptor() : m_trigger(), m_gateList() {}

            //! Set the trigger marker
            void SetTriggerMarker(TriggerMarker const& t) { m_trigger = t; }
            //! Return a const reference to trigger marker.
            TriggerMarker const& GetTriggerMarker() const { return m_trigger; }
            //! Set the record-stop marker.
            void SetRecordStopMarker(RecordStopMarker const& recordStop);

            //! Return a const reference to record-stop marker
            RecordStopMarker const& GetRecordStopMarker() const { return m_recordStop; }

            //! Add a gate marker to gate marker list.
            void AddGate(GateMarker const& gate) { m_gateList.push_back(gate); }

            //! Return a const reference to the list of gate markers.
            GateList const& GetGateList() const { return m_gateList; }

            //! Return the number of samples associated with the gate stored in memory (including padding).
            int64_t GetStoredSampleCount(ProcessingParameters const& params) const
            {
                auto const& fct = [&params, this](int64_t size, GateMarker const& gate) { return size + gate.GetStoredSampleCount(params, m_recordStop); };
                return std::accumulate(m_gateList.begin(), m_gateList.end(), int64_t(0), fct);
            }

        private:
            TriggerMarker m_trigger;       //!< trigger marker.
            GateList m_gateList;           //!< list of gate markers.
            RecordStopMarker m_recordStop; //!< record stop marker.
        };

        //! Decoder class for marker stream.
        /*! The class is designed to process a certain number of records in perpetual streaming context. We might want to process
            more than one record in order to optimize data transfer time from instrument-to-host.

            Typical use of such class would be:

                 MarkerStreamDecoder markerStreamDecoder;

                 for(;;) // infinite loop
                 {
                     // keep decoding the number of records
                     while(stream.size() > 0 && GetAvailableRecordCount.Size() < MinimumNumberOfRecords)
                         recordQueue.DecodeNextMarker(stream);

                     // process records
                     auto const recordList = MarkerStreamDecoder.Take(MinimumNumberOfRecords);
                     for(auto const& record : recordList)
                     {
                        // do something with "recordDesc"
                     }
                 }*/
        class MarkerStreamDecoder
        {
        public:
            using MarkerStream = ArraySegment<int32_t>;
            using RecordDescriptorList = std::vector<RecordDescriptor>;

            //! ZeroSuppress decoding state
            enum class State
            {
                ExpectTrigger,
                ExpectGate,
                ExpectAlign
            };

            //! Decoding mode
            enum class Mode
            {
                Normal,         //! Normal mode (ZeroSuppress disabled)
                ZeroSuppress    //! ZeroSuppress mode.
            };

            explicit MarkerStreamDecoder(Mode mode)
                : m_recordQueue()
                , m_currentRecord()
                , m_mode(mode)
                , m_state(State::ExpectTrigger)
            {}

            //! Decode the next marker from the given input stream
            /*! Decoded markers are removed from the input stream. */
            void DecodeNextMarker(MarkerStream& stream);

            //! Pop the next record descriptor out from the queue.
            RecordDescriptor Pop();

            //! Take the next "count" record descriptors from the queue and return them as result.
            RecordDescriptorList Take(int count);

            //! Return the number of record descriptors in the queue.
            size_t GetAvailableRecordCount() const { return m_recordQueue.size(); }

        private:
            //! Decode the next marker from marker stream generated in normal mode (ZeroSuppress disabled).
            void DecodeNextMarkerNormalMode(MarkerStream& stream);
            //! Decode the next marker from marker stream generated in ZeroSuppress mode.
            void DecodeNextMarkerZeroSuppressMode(MarkerStream& stream);

            //! Expect a trigger marker from the input marker stream, decode it and return it as a result.
            static TriggerMarker DecodeTriggerMarker(MarkerStream&);
            //! Expect a gate start marker from the input marker stream, decode it and return it as a result.
            static GateStartMarker DecodeGateStartMarker(MarkerStream&);
            //! Expect a (gate or record) stop marker from the input marker stream, decode it and return it as a result.
            static StopMarker DecodeStopMarker(MarkerStream&);
            //! Expect a gate marker (start+stop) from the input marker stream, decode it and return it as a result.
            static GateMarker DecodeGateMarker(MarkerStream&);
            //! Expect a dummy gate marker and consume it from the input marker stream.
            static void WalkthroughDummyMarker(MarkerStream&);

        private: // helper static methods
            static bool IsTriggerMarkerTag(MarkerTag tag)
            {
                return (tag == MarkerTag::TriggerNormal) || (tag == MarkerTag::TriggerAverager);
            }

        private:
            std::list<RecordDescriptor> m_recordQueue;
            RecordDescriptor m_currentRecord;
            Mode m_mode;
            State m_state;
        };

        //! Extract a tag from a 32-bit header element.
        inline MarkerTag ExtractTag(int32_t header)
        {
            return MarkerTag(header & 0xff);
        }

        //! Return the number of samples stored in memory for all records in #recordList stored.
        inline int64_t GetStoredSampleCountForRecords(std::vector<RecordDescriptor> const& recordList, ProcessingParameters const& processingParams)
        {
            auto const& fct = [&processingParams](int64_t size, RecordDescriptor const& record) { return size + record.GetStoredSampleCount(processingParams); };
            return std::accumulate(recordList.begin(), recordList.end(), int64_t(0), fct);
        }

    }


    ///////////////////////////////////////////////////////////////////////////
    //
    // ArraySegment class member definitions
    //

    template <typename T>
    inline ArraySegment<T>::ArraySegment(container_type const& data, size_t offset, size_t count)
        : m_data(data)
        , m_offset(offset)
        , m_size(count)
    {
        if (data.size() < offset + count)
        {
            throw std::logic_error("Array segment definition exceeds array size: "
                "offset=" + ToString(offset) +
                ", count=" + ToString(count) +
                ", array size=" + ToString(m_data.size())
            );
        }
    }

    template <typename T>
    inline void ArraySegment<T>::PopFront(size_t nbrElements)
    {
        if (Size() < nbrElements)
            throw std::invalid_argument("Cannot pop " + ToString(nbrElements) + " elements out from a segment of " + ToString(Size()) + ".");

        m_offset += nbrElements;
        m_size -= nbrElements;
    }


    ///////////////////////////////////////////////////////////////////////////
    //
    // StandardStreaming member definitions
    //

    inline TriggerMarker StandardStreaming::DecodeTriggerMarker(MarkerStream& stream)
    {
        uint32_t const header = stream[0];
        MarkerTag const tag = ExtractTag(header);

        if (!IsTriggerMarkerTag(tag))
            throw std::runtime_error("Expected trigger marker, got " + ToString(int(tag)));

        uint32_t const low = stream[1];
        uint32_t const high = stream[2];

        TriggerMarker result;

        result.tag = tag;
        result.recordIndex = (header >> 8) & TriggerMarker::RecordIndexMask;

        result.triggerTimeSamples = -(double(low & 0x000000ff) / 256.0);
        uint64_t const timestampLow = (low >> 8) & 0x0000000000ffffffL;
        uint64_t const timestampHigh = uint64_t(high) << 24;
        result.absoluteSampleIndex = timestampHigh | timestampLow;

        stream.PopFront(16);

        return result;
    }


    ///////////////////////////////////////////////////////////////////////////
    //
    // ZeroSuppress::GateMarker::GateStartMarker member definitions
    //

    inline ZeroSuppress::GateStartMarker::GateStartMarker(int32_t element0, int32_t element1)
        : m_blockIndex(0)
        , m_startSampleIndex(0)
    {
        MarkerTag const tag = ExtractTag(element0);
        if (tag != MarkerTag::GateStartCst)
            throw std::invalid_argument("Expected gate start tag, got " + ToString(int(tag)));

        auto const blockIndex = GateMarker::ExtractPosition(element0, element1);
        if (blockIndex < 1)
        {
            std::ostringstream oss;
            oss << "Start block index must be strict positive, got " << blockIndex
                << "(marker[0]=0x" << std::setw(8) << std::setfill('0') << element0
                << ", marker[1]=0x" << std::setw(8) << std::setfill('0') << element1 << ")";

            throw std::invalid_argument(oss.str());
        }

        m_blockIndex = blockIndex;
        m_startSampleIndex = (element1 >> 24) & 0xff;
    }

    ///////////////////////////////////////////////////////////////////////////
    //
    // ZeroSuppress::GateMarker::GateStopMarker member definitions
    //

    inline ZeroSuppress::StopMarker::StopMarker(int32_t element0, int32_t element1)
        : m_blockIndex(0)
        , m_gateEndIndex(0)
        , m_tag(MarkerTag::None)
    {
        MarkerTag const tag = ExtractTag(element0);

        if (tag != MarkerTag::GateStopCst && tag != MarkerTag::RecordStop)
            throw std::invalid_argument("Expected gate stop or record stop tags from header, got " + ToString(int(tag)));

        uint64_t const blockIndex = GateMarker::ExtractPosition(element0, element1);
        if (blockIndex < 1)
        {
            std::ostringstream oss;
            oss << "Start block index must be strict positive, got " << blockIndex
                << "(marker[0]=0x" << std::setw(8) << std::setfill('0') << element0
                << ", marker[1]=0x" << std::setw(8) << std::setfill('0') << element1 << ")";

            throw std::invalid_argument(oss.str());
        }

        m_blockIndex = blockIndex;
        m_gateEndIndex = (element1 >> 24) & 0xff;
        m_tag = tag;
    }

    inline ZeroSuppress::StopMarker::StopMarker()
        : m_blockIndex(1)
        , m_gateEndIndex(0)
        , m_tag(MarkerTag::RecordStop)
    {}

    ///////////////////////////////////////////////////////////////////////////
    //
    // GateMarker member definitions
    //

    inline int64_t ZeroSuppress::GateMarker::ExtractPosition(int32_t element0, int32_t element1)
    {
        int64_t const low = (element0 >> 24) & 0xff;
        int64_t const high = int64_t(element1 & 0xffffff) << 8;
        return high | low;
    }

    inline ZeroSuppress::GateMarker::GateMarker(GateStartMarker const& startMarker, GateStopMarker const& stopMarker)
        : m_startMarker(startMarker)
        , m_stopMarker(stopMarker)
    {
        if (m_stopMarker.GetBlockIndex() < m_startMarker.GetBlockIndex())
        {
            /* This might be caused by am overflow of the block index counter. In such case, "stop.m_blockIndex" must be
               incremented by 2**32 (recordIndex counter is 32-bit)*/
            throw std::invalid_argument("Gate start block index " + ToString(m_startMarker.GetBlockIndex()) + " exceeds stop block index " + ToString(m_stopMarker.GetBlockIndex()) + ".");
        }
    }

    inline int64_t ZeroSuppress::GateMarker::GetStoredSampleCount(ProcessingParameters const& params, RecordStopMarker const& recordStop) const
    {
        int64_t const gateBlocks = size_t(GetStopMarker().GetBlockIndex() - GetStartMarker().GetBlockIndex());
        int64_t const postGateRecordBlocks = recordStop.GetBlockIndex() - GetStopMarker().GetBlockIndex();

        if (postGateRecordBlocks < 0)
            throw std::invalid_argument("Block index of record-stop " + ToString(recordStop.GetBlockIndex()) + " is smaller than block index of gate-stop " + ToString(GetStopMarker().GetBlockIndex()) + ".");

        int64_t const gateSamples = gateBlocks * params.processingBlockSamples;

        int64_t const postGateRecordSamples = postGateRecordBlocks * params.processingBlockSamples;
        // additional pre and post-gate samples are not stored if they are acquired after the record-stop (end of the record).
        int64_t const additionalSamples = (std::min)(postGateRecordSamples, int64_t(params.preGateSamples + params.postGateSamples));
        return AlignUp<int64_t>(gateSamples + additionalSamples, params.storageBlockSamples);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    // GateMarker member definitions
    //

    inline void ZeroSuppress::RecordDescriptor::SetRecordStopMarker(RecordStopMarker const& recordStop)
    {
        if (!recordStop.IsRecordStop())
            throw std::invalid_argument("Expected record-stop marker");

        m_recordStop = recordStop;
    }

    ///////////////////////////////////////////////////////////////////////////
    //
    // MarkerStreamDecoder member definitions
    //

    inline ZeroSuppress::RecordDescriptor ZeroSuppress::MarkerStreamDecoder::Pop()
    {
        if (m_recordQueue.empty())
            throw std::logic_error("Cannot pop record descriptor from empty queue");

        RecordDescriptor result = m_recordQueue.front();
        m_recordQueue.pop_front();
        return result;
    }

    inline ZeroSuppress::MarkerStreamDecoder::RecordDescriptorList ZeroSuppress::MarkerStreamDecoder::Take(int count)
    {
        if (count <= 0)
            throw std::invalid_argument("Number of record descriptors to take must be strict positive number, got " + ToString(count));

        if (GetAvailableRecordCount() < size_t(count))
            throw std::invalid_argument("Cannot take " + ToString(count) + " record descriptors, only " + ToString(GetAvailableRecordCount()) + " are available");

        RecordDescriptorList result;
        result.reserve(count);

        for (int i = 0; i < count; ++i)
            result.push_back(Pop());

        return result;
    }

    inline void ZeroSuppress::MarkerStreamDecoder::WalkthroughDummyMarker(MarkerStream& stream)
    {
        MarkerTag const tag = ExtractTag(stream[0]);

        if (tag != MarkerTag::DummyGate)
            throw std::logic_error("Expected Dummy gate marker, got " + ToString(int(tag)));

        stream.PopFront(2);
    }

    inline ZeroSuppress::GateMarker ZeroSuppress::MarkerStreamDecoder::DecodeGateMarker(MarkerStream& stream)
    {
        GateStartMarker const start = DecodeGateStartMarker(stream);
        GateStopMarker const stop = DecodeStopMarker(stream);
        return GateMarker(start, stop);
    }

    inline ZeroSuppress::GateStartMarker ZeroSuppress::MarkerStreamDecoder::DecodeGateStartMarker(MarkerStream& stream)
    {
        GateStartMarker const result = GateStartMarker(stream[0], stream[1]);

        stream.PopFront(2);

        return result;
    }

    inline ZeroSuppress::StopMarker ZeroSuppress::MarkerStreamDecoder::DecodeStopMarker(MarkerStream& stream)
    {
        StopMarker const result = StopMarker(stream[0], stream[1]);

        stream.PopFront(2);

        return result;
    }

    inline TriggerMarker ZeroSuppress::MarkerStreamDecoder::DecodeTriggerMarker(MarkerStream& stream)
    {
        uint32_t const header = stream[0];
        MarkerTag const tag = ExtractTag(header);

        if (!IsTriggerMarkerTag(tag))
            throw std::runtime_error("Expected trigger marker, got " + ToString(int(tag)));

        uint32_t const low = stream[1];
        uint32_t const high = stream[2];

        TriggerMarker result;

        result.tag = tag;
        result.recordIndex = (header >> 8) & TriggerMarker::RecordIndexMask;

        result.triggerTimeSamples = -(double(low & 0x000000ff) / 256.0);
        uint64_t const timestampLow = (low >> 8) & 0x0000000000ffffffL;
        uint64_t const timestampHigh = uint64_t(high) << 24;
        result.absoluteSampleIndex = timestampHigh | timestampLow;

        stream.PopFront(16);

        return result;
    }

    inline void ZeroSuppress::MarkerStreamDecoder::DecodeNextMarker(MarkerStream& stream)
    {
        if (stream.Size() == 0)
            throw std::runtime_error("Cannot decode markers from an empty stream");

        if (m_mode == Mode::ZeroSuppress)
            DecodeNextMarkerZeroSuppressMode(stream);
        else if (m_mode == Mode::Normal)
            DecodeNextMarkerNormalMode(stream);
        else
            throw std::logic_error("Unexpected operating mode for MarkerStreamDecoder: " + ToString(int(m_mode)));
    }

    inline void ZeroSuppress::MarkerStreamDecoder::DecodeNextMarkerNormalMode(MarkerStream& stream)
    {
        // In Normal acquisition mode, only trigger markers are issued.
        auto recordDescriptor = RecordDescriptor();
        recordDescriptor.SetTriggerMarker(DecodeTriggerMarker(stream));
        m_recordQueue.push_back(recordDescriptor);
    }

    inline void ZeroSuppress::MarkerStreamDecoder::DecodeNextMarkerZeroSuppressMode(MarkerStream& stream)
    {
        if (m_state == State::ExpectTrigger || m_state == State::ExpectAlign)
        {
            // walkthrough all dummy align markers
            for (;;)
            {
                if (stream.Size() == 0)
                    return;

                MarkerTag const tag = ExtractTag(stream[0]);
                if (tag != MarkerTag::DummyGate)
                    break;

                WalkthroughDummyMarker(stream);
            }

            m_currentRecord = RecordDescriptor();
            m_currentRecord.SetTriggerMarker(DecodeTriggerMarker(stream));
            m_state = State::ExpectGate;
        }
        else if (m_state == State::ExpectGate)
        {
            MarkerTag const tag = ExtractTag(stream[0]);

            bool isRecordStop = false;
            if (tag == MarkerTag::GateStartCst)
            {
                auto const& gate = DecodeGateMarker(stream);
                m_currentRecord.AddGate(gate);
                isRecordStop = gate.GetStopMarker().IsRecordStop();

                if (gate.GetStopMarker().IsRecordStop())
                    m_currentRecord.SetRecordStopMarker(gate.GetStopMarker());
            }
            else if (tag == MarkerTag::RecordStop)
            {
                auto const stop = DecodeStopMarker(stream);
                m_currentRecord.SetRecordStopMarker(stop);
                isRecordStop = stop.IsRecordStop();
            }
            else
                throw std::runtime_error("Expected gate marker but got marker with tag value: " + ToString(int(tag)));

            if (isRecordStop)
            {
                m_recordQueue.push_back(m_currentRecord);
                m_state = State::ExpectAlign;
            }
        }
        else
            throw std::runtime_error("Unexpected ZeroSuppress decoding state: " + ToString(int(m_state)));
    }

}

#endif
