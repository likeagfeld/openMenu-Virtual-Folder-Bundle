using System;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using ByteSizeLib;

namespace GDMENUCardManager.Core
{

    public sealed class GdItem : INotifyPropertyChanged
    {
        public static int namemaxlen = 256;
        public static int serialmaxlen = 12;
        public static int foldermaxlen = 512;

        public string Guid { get; set; }

        private ByteSize _Length;
        public ByteSize Length
        {
            get { return _Length; }
            set { _Length = value; RaisePropertyChanged(); }
        }

        //public long CdiTarget { get; set; }

        private string _Name;
        public string Name
        {
            get { return _Name; }
            set
            {
                _Name = value;
                if (_Name != null)
                {
                    if (_Name.Length > namemaxlen)
                        _Name = _Name.Substring(0, namemaxlen);
                    _Name = Helper.RemoveDiacritics(_Name).Replace("_", " ").Trim();
                }

                RaisePropertyChanged();
            }
        }
        
        private string _ProductNumber;
        public string ProductNumber
        {
            get { return _ProductNumber; }
            set
            {
                _ProductNumber = value;
                if (_ProductNumber != null)
                {
                    if (_ProductNumber.Length > serialmaxlen)
                        _ProductNumber = _ProductNumber.Substring(0, serialmaxlen);
                    //todo check if this is needed
                    //_ProductNumber = Helper.RemoveDiacritics(_ProductNumber).Replace("_", " ").Trim();
                }

                RaisePropertyChanged();
            }
        }

        private string _Folder;
        public string Folder
        {
            get { return _Folder; }
            set
            {
                _Folder = value;
                if (_Folder != null)
                {
                    // Split by path separator and process each segment
                    var segments = _Folder.Split(new[] { '\\' }, StringSplitOptions.None);

                    for (int i = 0; i < segments.Length; i++)
                    {
                        // Trim whitespace from each segment
                        segments[i] = segments[i].Trim();

                        // Limit each segment to 39 characters (same as game name limit)
                        if (segments[i].Length > namemaxlen)
                        {
                            segments[i] = segments[i].Substring(0, namemaxlen);
                        }
                    }

                    // Remove empty segments (caused by double backslashes, leading/trailing backslashes)
                    segments = segments.Where(s => !string.IsNullOrEmpty(s)).ToArray();

                    // Rejoin with backslashes
                    _Folder = string.Join("\\", segments);

                    // Ensure total path length doesn't exceed 512 characters
                    if (_Folder.Length > foldermaxlen)
                    {
                        _Folder = _Folder.Substring(0, foldermaxlen);
                    }
                }

                RaisePropertyChanged();
            }
        }

        //private string _ImageFile;
        public string ImageFile
        {
            get { return ImageFiles.FirstOrDefault(); }
            //set { _ImageFile = value; RaisePropertyChanged(); }
        }

        public readonly System.Collections.Generic.List<string> ImageFiles = new System.Collections.Generic.List<string>();

        private string _FullFolderPath;
        public string FullFolderPath
        {
            get { return _FullFolderPath; }
            set { _FullFolderPath = value; RaisePropertyChanged(); }
        }

        private IpBin _Ip;
        public IpBin Ip
        {
            get { return _Ip; }
            set { _Ip = value; RaisePropertyChanged(); }
        }

        private int _SdNumber;
        public int SdNumber
        {
            get { return _SdNumber; }
            set { _SdNumber = value; RaisePropertyChanged(); RaisePropertyChanged(nameof(Location)); }
        }

        private WorkMode _Work;
        public WorkMode Work
        {
            get { return _Work; }
            set { _Work = value; RaisePropertyChanged(); }
        }

        public string Location
        {
            get { return SdNumber == 0 ? "Other" : "SD Card"; }
        }

        public bool CanApplyGDIShrink { get; set; }

        private FileFormat _FileFormat;
        public FileFormat FileFormat
        {
            get { return _FileFormat; }
            set { _FileFormat = value; RaisePropertyChanged(); }
        }

        private string _DiscType = "Game";
        public string DiscType
        {
            get { return _DiscType; }
            set { _DiscType = value; RaisePropertyChanged(); }
        }

        public string GetDiscTypeFileValue()
        {
            switch (DiscType)
            {
                case "Game": return "game";
                case "Other": return "other";
                default: return "game";
            }
        }

        public static string GetDiscTypeDisplayValue(string fileValue)
        {
            if (string.IsNullOrWhiteSpace(fileValue))
                return "Game";

            switch (fileValue.ToLower().Trim())
            {
                case "game": return "Game";
                case "other": return "Other";
                default: return "Game";
            }
        }

#if DEBUG
        public override string ToString()
        {
            return $"{Location} {SdNumber} {Name}";
        }
#endif

        public event PropertyChangedEventHandler PropertyChanged;

        private void RaisePropertyChanged([CallerMemberName] string propertyName = "")
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}
