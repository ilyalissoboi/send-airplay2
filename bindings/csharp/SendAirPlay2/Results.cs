// SPDX-License-Identifier: Apache-2.0
using System;
using System.Runtime.InteropServices;

namespace SendAirPlay2
{
    /// <summary>The SAP2_* result codes of playback.h.</summary>
    public enum ResultCode
    {
        /// <summary>SAP2_OK.</summary>
        Ok = 0,
        /// <summary>Null pointer, bad size or an out-of-range option.</summary>
        InvalidArgument = 1,
        /// <summary>The call is not valid in the handle's current phase.</summary>
        InvalidState = 2,
        /// <summary>A stop or a cancelled PIN entry interrupted the operation.</summary>
        Cancelled = 3,
        /// <summary>No stored credentials under that profile; pair first.</summary>
        ProfileNotFound = 4,
        /// <summary>The credential store is unavailable or its record is malformed.</summary>
        CredentialStore = 5,
        /// <summary>No credential store exists on this platform yet.</summary>
        Unsupported = 6,
        /// <summary>Peer verification, record authentication or the PIN failed.</summary>
        Authentication = 7,
        /// <summary>The receiver answered with a non-2xx status.</summary>
        ReceiverRejected = 8,
        /// <summary>No confirmed playing state before the start deadline.</summary>
        StartTimeout = 9,
        /// <summary>Network failure, timeout or peer disconnection.</summary>
        Connection = 10,
        /// <summary>Malformed or unexpected receiver message.</summary>
        Protocol = 11,
        /// <summary>The local media server could not start.</summary>
        MediaServer = 12,
        /// <summary>The receiver is no longer playing our item.</summary>
        NotOwned = 13,
        /// <summary>The receiver rejected or did not answer a command.</summary>
        CommandFailed = 14,
        /// <summary>The session already ended or is being stopped.</summary>
        Ended = 15,
        /// <summary>The library ran out of memory.</summary>
        OutOfMemory = 16,
        /// <summary>Unexpected backend failure; details are not exposed.</summary>
        Internal = 17,
        /// <summary>Pairing refused: the profile already has credentials.</summary>
        ProfileExists = 18,
        /// <summary>The PIN arrived after the PIN timeout.</summary>
        PinTimeout = 19,
        /// <summary>HLS remux: the container, codec or size cannot be remuxed.</summary>
        MediaUnsupported = 20,
        /// <summary>HLS remux: the container's structure is invalid.</summary>
        MediaMalformed = 21,
    }

    /// <summary>A failed library call, with its fixed result code.</summary>
    /// <remarks>Messages contain only the fixed result name, never receiver text,
    /// addresses, URLs, PINs or credentials.</remarks>
    public sealed class SendAirPlay2Exception : Exception
    {
        /// <summary>Creates the exception for a non-OK result.</summary>
        public SendAirPlay2Exception(ResultCode result)
            : base("send-airplay2: " + SendAirPlay2Library.ResultName(result))
        {
            Result = result;
        }

        /// <summary>The library's result code.</summary>
        public ResultCode Result { get; }

        /// <summary>Throws unless the native result is SAP2_OK.</summary>
        internal static void ThrowIfFailed(int result)
        {
            if (result != (int)ResultCode.Ok)
            {
                throw new SendAirPlay2Exception((ResultCode)result);
            }
        }
    }

    /// <summary>Library-wide information.</summary>
    public static class SendAirPlay2Library
    {
        /// <summary>The interface version this binding was written against.</summary>
        public const int BindingApiVersion = (int)Native.ApiVersion;

        /// <summary>The loaded native library's interface version.</summary>
        public static int ApiVersion => (int)Native.sap2_playback_api_version();

        /// <summary>The library's fixed name for a result code, such as "connection".</summary>
        public static string ResultName(ResultCode result)
        {
            var name = Native.sap2_result_name((int)result);
            return name == IntPtr.Zero ? "unknown" : Marshal.PtrToStringAnsi(name) ?? "unknown";
        }

        /// <summary>Throws when the loaded library is older than this binding.</summary>
        internal static void RequireCompatibleLibrary()
        {
            if (ApiVersion < BindingApiVersion)
            {
                throw new NotSupportedException(
                    "The loaded send_airplay2 library is older than this binding (API version " +
                    ApiVersion + ", need " + BindingApiVersion + ").");
            }
        }
    }

    /// <summary>Handle phase (SAP2_PHASE_*).</summary>
    public enum CastPhase
    {
        /// <summary>Created; not started.</summary>
        Created = 0,
        /// <summary>A start is in progress.</summary>
        Starting = 1,
        /// <summary>Playing session.</summary>
        Active = 2,
        /// <summary>The session ended by itself; call Stop or Dispose.</summary>
        Ended = 3,
        /// <summary>Start failed; see StartResult.</summary>
        StartFailed = 4,
        /// <summary>Stopped locally.</summary>
        Stopped = 5,
    }

    /// <summary>Last URL playback state reported by the receiver (SAP2_STATE_*).</summary>
    public enum PlaybackState
    {
        /// <summary>No state yet.</summary>
        None = 0,
        /// <summary>Loading.</summary>
        Loading = 1,
        /// <summary>Playing.</summary>
        Playing = 2,
        /// <summary>Paused.</summary>
        Paused = 3,
        /// <summary>Idle.</summary>
        Idle = 4,
        /// <summary>Stopped.</summary>
        Stopped = 5,
        /// <summary>Ended.</summary>
        Ended = 6,
        /// <summary>Any other reported state.</summary>
        Other = 7,
    }

    /// <summary>First terminal reason (SAP2_END_*).</summary>
    /// <remarks>A receiver-side Stop/Home has been observed to arrive as
    /// ConnectionLost; the library does not infer intent from closure.</remarks>
    public enum EndReason
    {
        /// <summary>Not ended.</summary>
        None = 0,
        /// <summary>Stopped by this host.</summary>
        SenderStop = 1,
        /// <summary>The media reached its end.</summary>
        MediaEnd = 2,
        /// <summary>The receiver reported a stop.</summary>
        ReceiverStop = 3,
        /// <summary>Another item took over the receiver's player.</summary>
        OwnershipLost = 4,
        /// <summary>A receiver connection failed.</summary>
        ConnectionLost = 5,
    }

    /// <summary>Local channel of the first failure (SAP2_FAILURE_CHANNEL_*).</summary>
    public enum FailureChannel
    {
        /// <summary>No failure.</summary>
        None = 0,
        /// <summary>URL event channel.</summary>
        UrlEvents = 1,
        /// <summary>Remote-control event channel.</summary>
        RemoteEvents = 2,
        /// <summary>URL feedback.</summary>
        UrlFeedback = 3,
        /// <summary>Remote-control feedback.</summary>
        RemoteFeedback = 4,
        /// <summary>Timing responder.</summary>
        Timing = 5,
        /// <summary>MRP data stream.</summary>
        Mrp = 6,
        /// <summary>Session supervisor.</summary>
        Supervisor = 7,
    }

    /// <summary>Fixed category of the first failure (SAP2_FAILURE_REASON_*).</summary>
    public enum FailureReason
    {
        /// <summary>No failure.</summary>
        None = 0,
        /// <summary>A deadline passed.</summary>
        Timeout = 1,
        /// <summary>The peer closed the connection.</summary>
        Disconnected = 2,
        /// <summary>Network error.</summary>
        Network = 3,
        /// <summary>Malformed message.</summary>
        InvalidMessage = 4,
        /// <summary>Authentication failed.</summary>
        Authentication = 5,
        /// <summary>Cancelled.</summary>
        Cancelled = 6,
        /// <summary>Rejected by the receiver.</summary>
        Rejected = 7,
        /// <summary>Any other failure.</summary>
        Other = 8,
    }

    /// <summary>A snapshot of one cast (sap2_cast_status).</summary>
    /// <remarks>Phase and start fields are read together; session and MRP fields
    /// are each copied under their own lock. Position is estimated between
    /// receiver updates and is not visual proof of playback.</remarks>
    public readonly struct CastStatus
    {
        internal CastStatus(in Native.CastStatus status)
        {
            Phase = (CastPhase)status.Phase;
            StartResult = (ResultCode)status.StartResult;
            RejectedStatus = (int)status.RejectedStatus;
            PlaybackState = (PlaybackState)status.PlaybackState;
            EndReason = (EndReason)status.EndReason;
            FailureChannel = (FailureChannel)status.FailureChannel;
            FailureReason = (FailureReason)status.FailureReason;
            Owned = status.Owned != 0;
            AtEnd = status.AtEnd != 0;
            CleanedUp = status.CleanedUp != 0;
            PositionSeconds = status.HasPosition != 0 ? status.PositionSeconds : (double?)null;
            DurationSeconds = status.HasDuration != 0 ? status.DurationSeconds : (double?)null;
            PlaybackRate = status.HasPlaybackRate != 0 ? status.PlaybackRate : (double?)null;
        }

        /// <summary>Handle phase.</summary>
        public CastPhase Phase { get; }
        /// <summary>Ok until a start finishes; then its result.</summary>
        public ResultCode StartResult { get; }
        /// <summary>HTTP status for ReceiverRejected, otherwise 0.</summary>
        public int RejectedStatus { get; }
        /// <summary>Last URL playback state.</summary>
        public PlaybackState PlaybackState { get; }
        /// <summary>First terminal reason.</summary>
        public EndReason EndReason { get; }
        /// <summary>Channel of the first failure.</summary>
        public FailureChannel FailureChannel { get; }
        /// <summary>Category of the first failure.</summary>
        public FailureReason FailureReason { get; }
        /// <summary>The receiver reports our item as its active player.</summary>
        public bool Owned { get; }
        /// <summary>The receiver reports a paused or stopped position at the duration.</summary>
        public bool AtEnd { get; }
        /// <summary>Session workers joined and channel secrets erased.</summary>
        public bool CleanedUp { get; }
        /// <summary>Estimated position, if known.</summary>
        public double? PositionSeconds { get; }
        /// <summary>Duration, if known.</summary>
        public double? DurationSeconds { get; }
        /// <summary>Playback rate, if known.</summary>
        public double? PlaybackRate { get; }
    }
}
